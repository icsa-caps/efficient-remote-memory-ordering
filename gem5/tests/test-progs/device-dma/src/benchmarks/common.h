#pragma once

#include <stdbool.h>
#include <inttypes.h>

#define CLSIZE 64
#define ALIGNED(x) ((((uintptr_t)src) & (CLSIZE - 1)) == 0)

void panic(const char* s);
