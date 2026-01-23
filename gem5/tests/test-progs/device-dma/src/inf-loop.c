#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

// #define CHECK_FUNCTIONALITY

#define ADDR_DMA_DEVICE 0xC0000000

#define OFFSET_DMA_TARGET_ADDRS 1
#define OFFSET_DMA_TRANSFR_SIZE 2
#define OFFSET_DMA_TRANSFER_DIR 3

// #define BUFFER_FILE_NAME "buffer.txt"

#define TOTAL_MEMORY (1 << 30)
uint8_t buf[TOTAL_MEMORY];

int main(void) {
    uint64_t* dev_ptr = (uint64_t*) ADDR_DMA_DEVICE;
    // struct stat sb;

    // int buf_fd = open(BUFFER_FILE_NAME, O_RDWR);
    // if (buf_fd == -1) {
    //     printf("Setup process: Open buffer file failed\n");
    //     perror("Setup process: open() error");
    //     exit(EXIT_FAILURE);
    // }
    // if (fstat(buf_fd, &sb) == -1) {
    //     perror("Setup process: fstat() error");
    //     exit(EXIT_FAILURE);
    // }
    // uint8_t* buf = (uint8_t*) mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    // if (buf == MAP_FAILED) {
    //     printf("Setup process: mmap failed\n");
    //     perror("Setup process: mmap() error");
    //     if (close(buf_fd) == -1) {
    //         printf("Setup process: Close buffer file failed\n");
    //         perror("Setup process: close() error");
    //         exit(EXIT_FAILURE);
    //     }
    //     exit(EXIT_FAILURE);
    // }
    // if (close(buf_fd) == -1) {
    //     printf("Setup process: Close buffer file failed\n");
    //     perror("Setup process: close() error");
    //     exit(EXIT_FAILURE);
    // }

    for (int i = 0; i < 512; i += 8) {
        buf[i] = 1;
    }
    
    *(dev_ptr + OFFSET_DMA_TARGET_ADDRS) = (uint64_t) buf;
    // *(dev_ptr + OFFSET_DMA_TRANSFR_SIZE) = NUM_UINT32 * sizeof(uint32_t);
    *(dev_ptr + OFFSET_DMA_TRANSFR_SIZE) = TOTAL_MEMORY;
    // printf("Transfer set up complete\n");
    // printf("Setup: Buffer start address: %lx\n", (uint64_t) buf);

#ifdef CHECK_FUNCTIONALITY
    uint32_t* read_ptr = (uint32_t*)(&(buf[128]));
    while (*read_ptr == 0);
    for (int i = 0; i < 32; ++i) {
        printf("Address %x, Value %lx\n", i*8, *((uint64_t*)(&(buf[i*8]))));
    }
#else
    while (1);
#endif

    return 1;
}

