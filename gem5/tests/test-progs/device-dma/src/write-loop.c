#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#define WRITE_INTERVAL 1

// #define ADDR_DMA_DEVICE 0xC0000000

// #define OFFSET_DMA_TARGET_ADDRS 1
// #define OFFSET_DMA_TRANSFR_SIZE 2
// #define OFFSET_DMA_TRANSFER_DIR 3

#define BUFFER_FILE_NAME "buffer.txt"

#define TOTAL_MEMORY (1 << 30)

int main(void) {
    uint32_t write_count = 1;
    uint8_t data = 0;

    // struct stat sb;

    // uint64_t* dev_ptr = (uint64_t*) ADDR_DMA_DEVICE;
    // while (*(dev_ptr + OFFSET_DMA_TRANSFR_SIZE) != TOTAL_MEMORY);
    // uint64_t* buf = (uint64_t*) (*(dev_ptr + OFFSET_DMA_TARGET_ADDRS));

    uint8_t* buf = (uint8_t*) 0x00008000;

    // int buf_fd = open(BUFFER_FILE_NAME, O_RDWR);
    // if (buf_fd == -1) {
    //     printf("Write process: Open buffer file failed\n");
    //     perror("Write process: open() error");
    //     exit(EXIT_FAILURE);
    // }
    // if (fstat(buf_fd, &sb) == -1) {
    //     perror("Write process: fstat() error");
    //     exit(EXIT_FAILURE);
    // }
    // uint8_t* buf = (uint8_t*) mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    // if (buf == MAP_FAILED) {
    //     printf("Write process: mmap failed\n");
    //     perror("Write process: mmap() error");
    //     if (close(buf_fd) == -1) {
    //         printf("Write process: Close buffer file failed\n");
    //         perror("Write process: close() error");
    //         exit(EXIT_FAILURE);
    //     }
    //     exit(EXIT_FAILURE);
    // }
    // if (close(buf_fd) == -1) {
    //     printf("Write process: Close buffer file failed\n");
    //     perror("Write process: close() error");
    //     exit(EXIT_FAILURE);
    // }

    // printf("Buffer start address: %lx\n", (uint64_t) buf);

    while (1) {
        if (write_count == WRITE_INTERVAL) {
            // Update data and cache line
            ++data;
            buf[193] = data;
            // printf("Wrote data %d to address %lx\n", data, (uint64_t)(&(buf[0])));
            // Reset count
            write_count = 1;
        } else {
            ++write_count;
        }
    }

    return 1;
}


