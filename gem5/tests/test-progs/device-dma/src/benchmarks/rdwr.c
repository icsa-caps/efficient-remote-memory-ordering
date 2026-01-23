// Cell data structure simulation in CPU

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "common.h"
#include "cell.h"
#include "cell-dev-addr-map.h"

uint8_t mem[CELL_SIZE];

struct stats {
  uint64_t writeSuccess;
  uint64_t writeFail;
  uint64_t readSuccess;
  uint64_t readFail;
};

struct args {
  bool reader;
  struct stats *stats;
};

void print_buf(char* buf, uint32_t b_size) {
  uint64_t* dptr = (uint64_t*) buf;
  uint32_t num_iters = b_size / sizeof(uint64_t);
  printf("CPU Buffer: ");
  for (uint32_t i = 0; i < num_iters; ++i) {
    printf("%016lx ", *dptr);
    ++dptr;
  }
  printf("\n");
}

void *run(void *argsv) {
  struct args *args = argsv;
  bool reader = args->reader;

  char buf[CELL_SIZE];
  uint64_t version = 0;

  // Initialize buffer
  uint64_t* bptr = (uint64_t*) buf;
  uint32_t num_iters = CELL_SIZE / sizeof(uint64_t);
  if (!reader) {
    for (uint32_t i = 0; i < num_iters; ++i) {
      // Note that the first successful write would have version number 2
      bptr[i] = 2; // version + 2
    }
  }

  while (true) {
    if (reader) {
      cell_read(&buf[0], mem);
    }
    else {
      cell_write_cpu(mem, &buf[0], &version);
#ifdef DEBUG_PRINT
      // Printing takes too many simulation ticks
      printf("Version %ld\n", version);
      print_buf(buf, CELL_SIZE);
#endif
      // Set up buf for next write
      for (uint32_t i = 0; i < num_iters; ++i) {
        // Successful writes always have an even version number
        bptr[i] += 2; // version + 2
      }
      // Delay
      // for (uint32_t i = 0; i < 100000; ++i);
    }
  }
}

int main() {
  // Initialize memory
  uint64_t* mptr = (uint64_t*) mem;
  uint32_t num_iters = CELL_SIZE / sizeof(uint64_t);
  for (uint32_t i = 0; i < num_iters; ++i) {
    mptr[i] = 0;
  }
  printf("Shared memory at %lx\n", (uint64_t) mem);

#ifdef CREATE_WRITER
  // Initialize arguments for CPU writer process
  struct stats stats = {0, 0, 0, 0};
  struct args p_args;
  p_args.stats = &stats;
  p_args.reader = 0;
#endif

  // Set up Cell device
  uint64_t* dev_ptr = (uint64_t*) ADDR_DMA_DEVICE;
  *(dev_ptr + CELL_DMA_MEM_ADDR_OFFSET) = (uint64_t) mem;
  *(dev_ptr + CELL_DMA_CELL_SIZE_OFFSET) = CELL_SIZE;
  *(dev_ptr + CELL_DMA_CTRL_REG_OFFSET) = 1;

#ifdef CREATE_WRITER
  // Start writer
  run((void*) (&p_args));
#endif

  // Write from dma device instead of cpu
  while (1);

  return 0;
}

