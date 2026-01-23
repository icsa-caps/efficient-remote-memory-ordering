#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#define ADDR_DMA_M_DEVICE 0xC0000000
#define ADDR_DMA_S_DEVICE 0xC0080000

#define OFFSET_DMA_TARGET_ADDRS 1
#define OFFSET_DMA_TRANSFR_SIZE 2
#define OFFSET_DMA_CTRL_REG     0

#define SDEVICE_MEMORY 0x80000

int main(void) {
    uint64_t* dev_ptr = (uint64_t*) ADDR_DMA_M_DEVICE;
    
    *(dev_ptr + OFFSET_DMA_TARGET_ADDRS) = (uint64_t) SDEVICE_MEMORY;
    *(dev_ptr + OFFSET_DMA_TRANSFR_SIZE) = SDEVICE_MEMORY;
    *(dev_ptr + OFFSET_DMA_CTRL_REG) = 1;

    while (1);

    return 1;
}

