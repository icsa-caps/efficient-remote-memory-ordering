#pragma once

#include <stddef.h>
#include <stdatomic.h>

void *dma_read(void *dst, void *src, size_t n, memory_order order);
void *dma_write(void *dst, void *src, size_t n, memory_order order);
