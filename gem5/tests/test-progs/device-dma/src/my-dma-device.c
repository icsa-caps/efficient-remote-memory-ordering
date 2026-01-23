#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define ADDR_DMA_DEVICE 0xC0000000

#define OFFSET_DMA_TARGET_ADDRL 1
#define OFFSET_DMA_TARGET_ADDRH 2
#define OFFSET_DMA_TRANSFR_SIZE 3
#define OFFSET_DMA_TRANSFER_DIR 4

int main(int argc, char* argv[])
{
    printf("Attempting to access dma device at address %x\n", ADDR_DMA_DEVICE);
    uint32_t* dev_ptr = (uint32_t*) ADDR_DMA_DEVICE;
    printf("DMA device magic number: %x\n", *dev_ptr);

    uint32_t* buf = (uint32_t*) malloc(10 * sizeof(uint32_t));
    if (buf == NULL) {
        printf("BIN_ERROR: Failed to allocate buffer memory.\n");
        return 1;
    }

    printf("Virtual address of allocated memory: %lx\n", buf);

    printf("Writing DMA read address...\n");
    *(dev_ptr + OFFSET_DMA_TARGET_ADDRL) = (uint32_t) buf;
    *(dev_ptr + OFFSET_DMA_TARGET_ADDRH) = 0;
    printf("DMA read addr: %x\n", *(dev_ptr + OFFSET_DMA_TARGET_ADDRL));
    printf("Writing DMA read size...\n");
    *(dev_ptr + OFFSET_DMA_TRANSFR_SIZE) = 1000 * sizeof(uint32_t);
    printf("DMA read size: %x\n", *(dev_ptr + OFFSET_DMA_TRANSFR_SIZE));
    printf("Starting DMA read...\n");
    *(dev_ptr + OFFSET_DMA_TRANSFER_DIR) = 1;
    printf("DMA read issued!\n");

    return 0;
}





