#include "common.h"
#include "dma.h"
#include "cell.h"

#include <stdio.h>

void cell_read(void *dstv, void *srcv) {
  uint64_t *dst = dstv;

  while (true) {
    uint64_t first_header;
    do {
      dma_read(dst, srcv, CELL_SIZE, memory_order_relaxed);
      first_header = dst[0];
    } while (first_header & 1);

    uint64_t second_header;
    dma_read(&second_header, srcv, sizeof(uint64_t), memory_order_relaxed);

    if (first_header == second_header)
      break;
  }
}

// All structure modifying operation are issued from the CPU
void cell_write_cpu(void *dst, void *src, uint64_t *version) {
  uint64_t ret = 0;

  atomic_uint_least64_t *header_version = dst;
  atomic_uint_least64_t *footer_version = header_version + (CELL_SIZE / sizeof(uint64_t)) - 1;
  atomic_uint_least64_t *dstw = dst;
  uint64_t *srcw = src;

#ifdef DEBUG_PRINT
  printf("header_version addr: %p, footer_version addr: %p\n", header_version, footer_version);
#endif

  while (true) {
    ret = atomic_compare_exchange_strong(header_version, version, *version + 1);

    if (ret == 0 || (*version & 1) == 1) {
      *version = *version + 1;
      continue;
    }

    break;
  }

  *version = *header_version;
  uint64_t const_val = *version + 1;
#ifdef DEBUG_PRINT
  printf("Acquired lock, version:%ld\n", *version);
#endif

  // write footer version
  atomic_store_explicit(footer_version, *version, memory_order_release);

#ifdef DEBUG_PRINT
  printf("Wrote footer, version:%ld, footer:%ld\n", *version, *footer_version);
#endif

  // Lock acquired, write to memory except the header and footer
  for (size_t i = 1; i < (((CELL_SIZE) / sizeof(uint64_t)) - 1); i++) {
    // atomic_store_explicit(&dstw[i], srcw[i], memory_order_relaxed);
    atomic_store_explicit(&dstw[i], const_val, memory_order_relaxed);
#ifdef DEBUG_PRINT
    printf("Version: %ld, i:%ld, dstw[i]: %ld, srcw[i]: %ld\n", *version, i, dstw[i], srcw[i]);
#endif
  }

  srcw[1] = const_val;

  // Write the version + 1 in the footer
  *version = *version + 1;
  atomic_store_explicit(footer_version, *version, memory_order_relaxed);
  
#ifdef DEBUG_PRINT
  printf("Updated footer, version:%ld, footer:%ld\n", *version, *footer_version);
#endif

  // Release the lock by writing the version + 1 in the header
  atomic_store_explicit(header_version, *version, memory_order_release);

#ifdef DEBUG_PRINT
  printf("Released lock, version:%ld, footer:%ld\n", *version, *header_version);
#endif
}

