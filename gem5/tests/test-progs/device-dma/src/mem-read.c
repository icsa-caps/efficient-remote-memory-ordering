#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define TOTAL_MEMORY 128 * 1024 * 1024

uint8_t buf[TOTAL_MEMORY];

int main(void) {
    uint32_t idx;
    uint8_t reg = 0;
    for (idx = 0; idx < TOTAL_MEMORY; idx += 8) {
        reg = buf[idx];
    }
    printf("Read complete...\n");
    return 0;
}

