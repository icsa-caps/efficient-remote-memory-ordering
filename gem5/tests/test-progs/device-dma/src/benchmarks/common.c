#include <stdio.h>
#include <stdlib.h>

#include "common.h"

void panic(const char* s) {
  perror(s);
  exit(-1);
}

