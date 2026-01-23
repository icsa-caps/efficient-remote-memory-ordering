#include <pthread.h>
#include <assert.h>
#include <inttypes.h>

#include "common.h"
#include "dma.h"

pthread_mutex_t dma_mutex;

// Assumes dst is private, src is shared.
void *dma_read(void *dst, void *src, size_t n, memory_order order) {
  assert(ALIGNED(src));
  assert(ALIGNED(dst));
  assert(ALIGNED(n));

  uint64_t *dstw = dst;
  atomic_uint_least64_t *srcw = src;
  assert(sizeof *srcw == sizeof(uint64_t));

  for (size_t i = 0; i < n; i++)
    dstw[i] = atomic_load_explicit(&srcw[i], memory_order_relaxed);

  atomic_thread_fence(order);
  return dst;
}

// Assumes dst is shared, src is private.
void *dma_write(void *dst, void *src, size_t n, __attribute__((unused)) memory_order order) {
  assert(ALIGNED(src));
  assert(ALIGNED(dst));
  assert(ALIGNED(n));

  int tr = pthread_mutex_lock(&dma_mutex); // implict acquire
  if (tr)
    panic("lock failed");

  atomic_uint_least64_t *dstw = dst;
  uint64_t *srcw = src;
  assert(sizeof *dstw == sizeof(uint64_t));

  for (size_t i = 0; i < n; i++)
    atomic_store_explicit(&dstw[i], srcw[i], memory_order_relaxed);

  tr = pthread_mutex_unlock(&dma_mutex); // implict release
  if (tr)
    panic("unlock failed");

  // no need for explicit barrier since unlock implies release

  return dst;
}

