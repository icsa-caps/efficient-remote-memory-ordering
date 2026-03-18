#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <signal.h>

#define DEFAULT_PORT "20079"
#define MAX_WR 8192

struct server_context {
  struct rdma_cm_id  *id;
  struct ibv_pd      *pd;
  struct ibv_mr      *mr;
  char               *buf;
};

static void *buffer;

static void die(const char *msg) {
  if (buffer)
    free(buffer);
  perror(msg);
  exit(EXIT_FAILURE);
}

static void signal_handler(int signum) {
  printf("Exiting...\n");
  if (buffer)
    free(buffer);
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  int rdma_read_size;
  int is_use_global_buffer = 0;

  /* Takes batch_size and rdma_read_size as commandline arguments */
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <rdma_read_size> <is_global_buf>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  rdma_read_size = atoi(argv[1]);
  is_use_global_buffer = atoi(argv[2]);

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  
  if (is_use_global_buffer) {
    buffer = malloc(rdma_read_size);
    if (!buffer) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    memset(buffer, 5, rdma_read_size);
  }

  struct addrinfo hints = { .ai_flags = AI_PASSIVE,
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM };
  struct addrinfo *res;
  if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &res))
    die("getaddrinfo");

  struct rdma_event_channel *ec = rdma_create_event_channel();
  if (!ec) die("rdma_create_event_channel");

  struct rdma_cm_id *listener;
  if (rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP))
    die("rdma_create_id");

  if (rdma_bind_addr(listener, res->ai_addr))
    die("rdma_bind_addr");
  freeaddrinfo(res);

  if (rdma_listen(listener, 10))
    die("rdma_listen");

  printf("rdma-server: listening on port %s\n", DEFAULT_PORT);

  while (1) {
    struct rdma_cm_event *event;
    if (rdma_get_cm_event(ec, &event))
      die("rdma_get_cm_event");

    struct rdma_cm_event ev = *event;
    rdma_ack_cm_event(event);

    if (ev.event == RDMA_CM_EVENT_CONNECT_REQUEST) {
      // allocate context
      struct server_context *ctx = calloc(1, sizeof(*ctx));
      ctx->id = ev.id;
      ev.id->context = ctx;

      // allocate & register a buffer
      if (is_use_global_buffer)
        ctx->buf = buffer;
      else
        ctx->buf = malloc(rdma_read_size);
      
      ctx->pd  = ibv_alloc_pd(ev.id->verbs);
      if (!ctx->pd) die("ibv_alloc_pd");
      ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, rdma_read_size,
          IBV_ACCESS_LOCAL_WRITE |
          IBV_ACCESS_REMOTE_READ | 
          IBV_ACCESS_REMOTE_WRITE |
          IBV_ACCESS_REMOTE_ATOMIC);
      if (!ctx->mr) die("ibv_reg_mr");

      // create a QP for this connection
      struct ibv_qp_init_attr qp_attr = {
        .cap        = { .max_send_wr  = MAX_WR,
          .max_recv_wr  = MAX_WR,
          .max_send_sge = 1,
          .max_recv_sge = 1 },
        .sq_sig_all = 0,
        .qp_type    = IBV_QPT_RC
      };
      if (rdma_create_qp(ev.id, ctx->pd, &qp_attr))
        die("rdma_create_qp");

      // pass addr + rkey in private_data
      struct {
        uint64_t addr;
        uint32_t rkey;
      } mem_info = {
        .addr = (uint64_t)ctx->buf,
        .rkey = ctx->mr->rkey
      };

      struct rdma_conn_param conn_param = { 0 };
      conn_param.responder_resources = 16; // HW Max
      conn_param.private_data        = &mem_info;
      conn_param.private_data_len    = sizeof(mem_info);

      if (rdma_accept(ev.id, &conn_param))
        die("rdma_accept");

      printf("rdma-server: accepted, sent MR info\n");

    } else if (ev.event == RDMA_CM_EVENT_DISCONNECTED) {
      struct server_context *ctx = ev.id->context;
      printf("rdma-server: client disconnected, cleaning up\n");
      rdma_destroy_qp(ev.id);
      ibv_dereg_mr(ctx->mr);
      ibv_dealloc_pd(ctx->pd);
      rdma_destroy_id(ev.id);
      if (!is_use_global_buffer)
        free(ctx->buf);
      free(ctx);
    }
  }

  if (is_use_global_buffer)
    free(buffer);

  return 0;
}
