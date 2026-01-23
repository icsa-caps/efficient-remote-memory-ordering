#pragma once

#include <stdint.h>

#define CELL_NBLKS 4
#define CELL_SIZE (CELL_NBLKS*CLSIZE)

void cell_read(void *dst, void *src);
void cell_write_cpu(void *dst, void *src, uint64_t *version);

// #define DEBUG_PRINT
