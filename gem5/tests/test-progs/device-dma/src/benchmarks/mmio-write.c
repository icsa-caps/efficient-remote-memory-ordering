#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <immintrin.h>

#define ADDR_MMIO_BASE 0xC0000000
#define MMIO_SIZE 0x40000

// #define TPUT_LOOP_SIZE 0x80000000 // Total bytes to transfer 2GB

#define TPUT_LOOP_SIZE 0xC00000

static inline unsigned long mmio_rdtsc(void)
{
    unsigned long low, high;

    asm volatile("rdtsc" : "=a" (low), "=d" (high) );

    return ((low) | (high) << 32);
}

// 3GHz
#define SIM_CPU_FREQ 3000000000

void send_msg(uint32_t msg_size) {
    unsigned volatile i, j, x;
    uint64_t start, tot_elapsed = 0, tot_bytes = 0;

    uint8_t* dev_ptr = (uint8_t*) ADDR_MMIO_BASE;

    uint32_t num_iters_i = TPUT_LOOP_SIZE / msg_size;
    uint32_t num_iters_j = msg_size / 64;

#ifdef USE_MFENCE
    printf("Starting MMIO mfence write experiment with message size %d\n", msg_size);
#else
    printf("Starting MMIO write experiment with message size %d\n", msg_size);
#endif

    x = 0;
    start = mmio_rdtsc();
    for (i = 0; i < num_iters_i; i++) {
        for (j = 0; j < num_iters_j; j++) {
            // printf("Writing to address %x\n", ADDR_MMIO_BASE + x);
            // asm volatile(
            //     "vmovdqa %%ymm1, (%0)\n" // Store ZMM0 into mmio_base
            //     :
            //     : "r"(ADDR_MMIO_BASE + x) // Input operands
            //     : "memory"           // Clobbered registers
            // );
            *(dev_ptr + x) = 68;
            // Align writes to 64 B
            x += 64;
            // Wrap back to zero from write limit
            x &= 0x0003FFFF;
        } 
#ifdef USE_MFENCE
        _mm_mfence();
#endif
    }
    tot_elapsed = (mmio_rdtsc() - start);
    tot_bytes = TPUT_LOOP_SIZE;
    printf("mmio_bench %u : transfered %lu bytes in %lu cycles\n", msg_size, tot_bytes, tot_elapsed);
}

void send_msg_unrolled(uint32_t msg_size) {
    unsigned volatile i, j, x;
    uint64_t start, tot_elapsed = 0, tot_bytes = 0;

    uint8_t* dev_ptr = (uint8_t*) ADDR_MMIO_BASE;
    uint32_t num_iters_i = TPUT_LOOP_SIZE / msg_size;

#ifdef USE_MFENCE
    printf("Starting MMIO mfence write experiment with message size %d\n", msg_size);
#else
    printf("Starting MMIO write experiment with message size %d\n", msg_size);
#endif

    x = 0;
    start = mmio_rdtsc();
    for (i = 0; i < num_iters_i; i++) {
        *(dev_ptr + x) = 68;
        *(dev_ptr + x + 64) = 68;
        *(dev_ptr + x + 128) = 68;
        *(dev_ptr + x + 192) = 68;
        *(dev_ptr + x + 256) = 68;
        *(dev_ptr + x + 320) = 68;
        *(dev_ptr + x + 384) = 68;
        *(dev_ptr + x + 448) = 68;
        x += 512;
        x &= 0x0003FFFF;
#ifdef USE_MFENCE
        _mm_mfence();
#endif
    }

    tot_elapsed = (mmio_rdtsc() - start);
    tot_bytes = TPUT_LOOP_SIZE;
    printf("mmio_bench %u : transfered %lu bytes in %lu cycles\n", msg_size, tot_bytes, tot_elapsed);
}

int main(void) {
    uint32_t msg_size = 64;
    // msg_size from 64B to 8192B
    uint32_t num_msgs = 8;

#ifdef USE_MFENCE
    send_msg(MFENCE_GRAN);
#else
    for (uint32_t i = 0; i < num_msgs; ++i) {
        send_msg(msg_size);
        msg_size = msg_size << 1;
    }
#endif

    // send_msg_unrolled(512);

    return 0;
}




