#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <stdbool.h>
#include <emmintrin.h>
#include <argp.h>

#define SERVER_IP    "10.10.1.1"
#define SERVER_PORT  "20079"

#define MAX_THREADS  64
#define BENCH_TIME 5
#define MAX_SAMPLES ((uint64_t)10000000)
#define MAX_QP 1024
#define MAX_WR 8192 

struct mem_info {
  uint64_t addr;
  uint32_t rkey;
};

struct client_context {
  struct rdma_cm_id  *id;
  struct ibv_pd      *pd;
  struct ibv_mr      *mr;
  char               *local_buf;
  struct rdma_event_channel *ec;
  struct mem_info mem_info;
  size_t buffer_size;
};

struct thread_arg {
  int thread_id;
  double cycles;
  int batch_size;
  int object_size;
  int num_qps;
  bool signaled;
  struct client_context **ctx;
};

static void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

// read the 64-bit time-stamp counter
static inline uint64_t rdtsc(void) {
  unsigned lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

double get_tsc_freq(int sleep_ms) {
  struct timespec t0, t1, req;
  uint64_t c0, c1;

  // get start wall-clock and TSC
  clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
  c0 = rdtsc();

  // sleep for sleep_ms milliseconds
  req.tv_sec  = sleep_ms / 1000;
  req.tv_nsec = (sleep_ms % 1000) * 1000000;
  nanosleep(&req, NULL);

  // get end wall-clock and TSC
  clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
  c1 = rdtsc();

  // compute elapsed seconds
  double dt = (t1.tv_sec  - t0.tv_sec)
    + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
  // rate = cycles / second
  return (double)(c1 - c0) / dt;
}

enum completion {
  signaled = true,
  unsignaled = false,
};

inline void postWrite(void* memAddr, size_t size, struct ibv_qp* qp, struct ibv_mr* mr, enum completion wc, size_t rkey, size_t remoteOffset, size_t wcId, bool needFence, int batch_size)
{
  struct ibv_send_wr sq_wr[batch_size];
  struct ibv_sge send_sgl[1];
  struct ibv_send_wr* bad_wr;
  
  send_sgl[0].addr = (uint64_t)(unsigned long)memAddr;
  send_sgl[0].length = size;
  send_sgl[0].lkey = mr->lkey;

  for (int i = 0; i < batch_size; i++) {
    sq_wr[i].opcode = IBV_WR_RDMA_WRITE;
    sq_wr[i].send_flags = 0;
    if (wc == signaled)
      sq_wr[i].send_flags |= IBV_SEND_SIGNALED;
    if (wc == unsignaled && i == (batch_size - 1))
      sq_wr[i].send_flags |= IBV_SEND_SIGNALED;  // make last wr signaled if others are not signaled
    if(needFence)
      sq_wr[i].send_flags |= IBV_SEND_FENCE;
    sq_wr[i].sg_list = &send_sgl[0];
    sq_wr[i].num_sge = 1;
    sq_wr[i].wr.rdma.rkey = rkey;
    sq_wr[i].wr.rdma.remote_addr = remoteOffset;
    sq_wr[i].wr_id = wcId;
    if (i < batch_size - 1)
      sq_wr[i].next = &sq_wr[i + 1];
    else
      sq_wr[i].next = NULL;
  }

  int ret = ibv_post_send(qp, sq_wr, &bad_wr);
  if (ret) {
    printf("FAILED: Submit write failed\n");  
    exit(EXIT_FAILURE);
  }
}

inline int pollCompletion(struct ibv_cq* cq, size_t expected, struct ibv_wc* wcReturn)
{
  int numCompletions = 0;
  numCompletions = ibv_poll_cq(cq, expected, wcReturn);
  if (numCompletions < 0) {
    printf("FAILED: Polling completions failed\n");
    exit(EXIT_FAILURE);
  }
  return numCompletions;
}

void connect_client(struct client_context *ctx) {
  struct rdma_cm_event *event;
  struct rdma_conn_param conn_param = { 0 };

  // create ID & event channel
  ctx->ec = rdma_create_event_channel();
  if (!ctx->ec) die("rdma_create_event_channel");
  if (rdma_create_id(ctx->ec, &ctx->id, ctx, RDMA_PS_TCP))
    die("rdma_create_id");

  // resolve addr
  struct sockaddr_in srv = {
    .sin_family = AF_INET,
    .sin_port   = htons(atoi(SERVER_PORT))
  };
  inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

  if (rdma_resolve_addr(ctx->id, NULL,
        (struct sockaddr*)&srv, 2000))
    die("rdma_resolve_addr");
  rdma_get_cm_event(ctx->ec, &event);
  rdma_ack_cm_event(event);

  // resolve route
  if (rdma_resolve_route(ctx->id, 2000))
    die("rdma_resolve_route");
  rdma_get_cm_event(ctx->ec, &event);
  rdma_ack_cm_event(event);

  // alloc PD & MR
  ctx->pd = ibv_alloc_pd(ctx->id->verbs);
  if (!ctx->pd) die("ibv_alloc_pd");
  ctx->local_buf = malloc(ctx->buffer_size);
  if (!ctx->local_buf) die("malloc");
  memset(ctx->local_buf, 0, ctx->buffer_size);
  ctx->mr = ibv_reg_mr(ctx->pd, ctx->local_buf, ctx->buffer_size,
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
  if (!ctx->mr) die("ibv_reg_mr");

  // create QP
  struct ibv_qp_init_attr qp_attr = {
    .cap        = { .max_send_wr  = MAX_WR,
      .max_recv_wr  = MAX_WR,
      .max_send_sge = 1,
      .max_recv_sge = 1 },
    .sq_sig_all = 0,
    .qp_type    = IBV_QPT_RC
  };

  if (rdma_create_qp(ctx->id, ctx->pd, &qp_attr))
    die("rdma_create_qp");

  // connect
  conn_param.initiator_depth = 16; // Doesn't matter for writes
  conn_param.responder_resources = 16; // Doesn't matter for writes
  conn_param.retry_count = 7;
  if (rdma_connect(ctx->id, &conn_param))
    die("rdma_connect");

  rdma_get_cm_event(ctx->ec, &event);
  if (event->event != RDMA_CM_EVENT_ESTABLISHED)
    die("expected ESTABLISHED");

  memcpy(&ctx->mem_info,
      event->param.conn.private_data,
      sizeof(struct mem_info));

  rdma_ack_cm_event(event);
}

void disconnect_client(struct client_context *ctx) {
  rdma_disconnect(ctx->id);
  rdma_destroy_qp(ctx->id);
  ibv_dereg_mr(ctx->mr);
  ibv_dealloc_pd(ctx->pd);
  rdma_destroy_id(ctx->id);
  rdma_destroy_event_channel(ctx->ec);
  free(ctx->local_buf);
  free(ctx);
}

void *run_benchmark(void *arg) {
  uint64_t start = rdtsc();
  uint64_t diff = 0;
  uint64_t op_start = 0;
  int req_num = 0;
  struct thread_arg *targ = (struct thread_arg *)arg;
  int thread_id = targ->thread_id;
  int batch_size = targ->batch_size / targ->num_qps;
  int rd_sz = targ->object_size;
  int num_qps = targ->num_qps;
  bool is_signaled = targ->signaled;
  struct client_context **ctx = targ->ctx;
  uint64_t *total_ops = (uint64_t *)malloc(sizeof(uint64_t));
  *total_ops = 0;

  while(diff < targ->cycles) {
    for (int i = 0; i < num_qps; i++) {
      postWrite(ctx[i]->local_buf, rd_sz, ctx[i]->id->qp, ctx[i]->mr, is_signaled, ctx[i]->mem_info.rkey, ctx[i]->mem_info.addr, 0, false, batch_size);
    }

    for (int i = 0; i < num_qps; i++) {
      int comp = 0;
      int tot_comp = 0;
      int tot_expected = is_signaled ? batch_size : 1;
      struct ibv_wc wcReturn[batch_size];
      while (tot_comp != tot_expected) {
        _mm_pause();
        int expected = tot_expected - tot_comp;
        comp = pollCompletion(ctx[i]->id->qp->send_cq, expected, wcReturn);
        for (int i = 0; i < comp; i++) {
          if (wcReturn[i].status != IBV_WC_SUCCESS) {
            printf("Thread %d, QP %d: Completion failed with status %s\n", thread_id, i, ibv_wc_status_str(wcReturn[i].status));
            exit(EXIT_FAILURE);
          }
        }
        tot_comp += comp;
      }
    }

    *total_ops += (batch_size * num_qps);
    diff = rdtsc() - start;
  }

  return (void *)total_ops;
}

const char *argp_program_version = "write_bench 1.0";
const char *argp_program_bug_address = "ashfaq@cs.utah.edu";

/* Program documentation. */
static char doc[] = "RDMA WRITE Benchmark Client\n"
"This program connects to a RDMA server and performs RDMA WRITEs.\n"
"It measures throughput and avg. latency of the write operations.";

/* A description of the arguments we accept (none in this case). */
static char args_doc[] = "";

/* Our option keys. */
enum {
  OPT_BATCH_SIZE = 'b',
  OPT_OBJECT_SIZE = 's',
  OPT_NUM_THREADS = 't',
  OPT_NUM_QP = 'q',
  OPT_SIGNALED = 'g',
};

/* Used by main to communicate with parse_opt. */
struct arguments {
  int batch_size;
  int object_size;
  int num_threads;
  int num_qps;
  bool signaled;
};

/* The options we understand. */
static struct argp_option options[] = {
  {"batch_size", OPT_BATCH_SIZE, "N",            0, "Size of the batch or pipeline (default: 1)" },
  {"object_size",  OPT_OBJECT_SIZE,  "N",            0, "Size of each RDMA read request in bytes (default: 64)" },
  {"num_threads", OPT_NUM_THREADS, "N", 0, "Number of threads to use (default: 1)"},
  {"num_qps", OPT_NUM_QP, "N", 0, "Number of QP to use (default: 1)"},
  {"signaled", OPT_SIGNALED,  NULL,       0, "Use signaled completions for all requests" },
  { 0 }
};

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = state->input;

  switch (key) {
    case OPT_BATCH_SIZE:
      arguments->batch_size = atoi(arg);
      if (arguments->batch_size <= 0) {
        argp_error(state, "batch_size must be a positive integer");
      }
      break;
    case OPT_OBJECT_SIZE:
      arguments->object_size = atoi(arg);
      if (arguments->object_size <= 0) {
        argp_error(state, "write_size must be a positive integer");
      }
      break;
    case OPT_NUM_THREADS:
      arguments->num_threads = atoi(arg);
      if (arguments->num_threads <= 0 || arguments->num_threads > MAX_THREADS) {
        argp_error(state, "num_threads must be a positive integer and less than or equal to %d", MAX_THREADS);
      }
      break;
    case OPT_NUM_QP:
      arguments->num_qps = atoi(arg);
      if (arguments->num_qps <= 0) {
        argp_error(state, "num_qps must be a positive integer and less than or equal to 1");
      }
      break;
    case OPT_SIGNALED:
      arguments->signaled = true;
      break;
    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char **argv) {
  struct arguments arguments = {16, 64, 1, 1, false};
  argp_parse(&argp, argc, argv, 0, 0, &arguments);
  printf("Batch size: %d\n", arguments.batch_size);
  printf("Object size: %d\n", arguments.object_size);
  printf("Number of threads: %d\n", arguments.num_threads);
  printf("Number of QPs: %d\n", arguments.num_qps);
  printf("Signaled: %s\n", arguments.signaled ? "true" : "false");
  
  struct client_context *ctx[MAX_QP];
  int n = arguments.num_threads;
  pthread_t th[n];
  uint64_t total_ops = 0;
  double tsc_freq = 0;

  tsc_freq = get_tsc_freq(1000);
  printf("TSC freq: %lf\n", tsc_freq);
  double cycles = tsc_freq * BENCH_TIME;

  // Create qp connections
  for (int i  = 0; i < arguments.num_qps; i++) {
    ctx[i] = (struct client_context *)malloc(sizeof(struct client_context));
    if (!ctx[i]) {
      die("malloc");
    }

    ctx[i]->buffer_size = arguments.object_size;

    connect_client(ctx[i]);
  }

  for (int i = 0; i < n; i++) {
    struct thread_arg *targ = (struct thread_arg *)malloc(sizeof(struct thread_arg));
    if (!targ)
      die("malloc");

    targ->thread_id = i;
    targ->cycles = cycles;
    targ->batch_size = arguments.batch_size;
    // targ->batch_size = 8192 / n;
    targ->object_size = arguments.object_size;
    targ->num_qps = arguments.num_qps;
    targ->signaled = arguments.signaled;
    targ->ctx = ctx;
    
    printf("Creating new thread...\n");
    if (pthread_create(&th[i], NULL, run_benchmark, targ))
      die("pthread_create");
  }
  
  void *th_ops;
  for (int i = 0; i < n; i++) {
    pthread_join(th[i], &th_ops);
    total_ops += *(uint64_t *)th_ops;
  }
  
  // teardown
  for (int i = 0; i < arguments.num_qps; i++) {
    disconnect_client(ctx[i]);
  }

  printf("Total ops: %lu\n", total_ops);

  // print throughput
  double elapsed = (double)cycles / tsc_freq;
  double throughput = (double)total_ops / elapsed;
  double gbps = (double)(total_ops * arguments.object_size * 8) / (elapsed * 1024 * 1024 * 1024);
  printf("Throughput: %.2f ops/sec\n", throughput);
  printf("Throughput: %.2f Gbps\n", gbps);
  printf("Avg. Latency: %.3f us\n", (elapsed * 1e6) / total_ops);

  return 0;
}
