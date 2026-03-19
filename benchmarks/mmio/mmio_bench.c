#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/set_memory.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <asm/barrier.h>
#include <linux/sched.h>
#include <asm/msr.h>
#include <asm/fpu/api.h>

#define MSR_PLATFORM_INFO 0xCE

#define LOOP 10000
#define MMIO_BASE 0x202ffc000000 // Replace with device MMIO base address
#define MMIO_OFFSET 0x1800000
#define MMIO_SIZE 0x40000      // Size of MMIO region (l1tlb reach: 256KB)

#define FIXED_TOTAL_BYTES
#define TPUT_LOOP_SIZE 0x80000000 // Total bytes to transfer 2GB
#define TPUT_LOOP_ITER 128

// Module parameters
static int bench_size = 0;

module_param(bench_size, int, 0644);

static void __iomem *mmio_base;

static inline unsigned long mmio_rdtsc(void)
{
    unsigned long low, high;

    asm volatile("rdtsc" : "=a" (low), "=d" (high) );

    return ((low) | (high) << 32);
}

static void print_cpu_clock_rate(void)
{
    u64 msr_value;
    unsigned int base_frequency; // Base frequency in MHz
    int cpu = smp_processor_id(); // Get the current CPU ID

    // Read the MSR_PLATFORM_INFO register
    if (!rdmsrl_safe(MSR_PLATFORM_INFO, &msr_value)) {
        base_frequency = (msr_value >> 8) & 0xFF; // Extract bits [15:8]
        printk(KERN_INFO "CPU %d Base Frequency: %u MHz\n", cpu, base_frequency);
    } else {
        printk(KERN_ERR "Failed to read MSR_PLATFORM_INFO on CPU %d\n", cpu);
    }
}

static int mmio_bench_tput(uint64_t size) {
  unsigned volatile i, j, x;
  unsigned int num_iters_i;
  unsigned int num_iters_j;
  uint64_t start, tot_elapsed = 0, tot_bytes = 0;

  pr_info("mmio_bench: Running mmio benchmark\n");
  if ((unsigned long)mmio_base % 32) {
    pr_err("mmio_bench: mmio_base is not 32-byte aligned\n");
    return -EINVAL;
  }
  
  x = 0;
  kernel_fpu_begin();
  start = mmio_rdtsc();
  for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
    for (j = 0; j < size / 32; j++) {
      asm volatile (
          "vmovdqa %%ymm1, (%0)\n"  // Store ZMM0 into mmio_base
          :
          : "r" (mmio_base + x)  // Input operands
          : "memory"          // Clobbered registers
      );
      x += 32;
      if (x >= MMIO_SIZE)
        x = 0;
    }
#if USE_SFENCE
    asm volatile ("sfence" ::: "memory");
#endif
  }
  tot_elapsed = (mmio_rdtsc() - start);
  kernel_fpu_end();
  tot_bytes = TPUT_LOOP_SIZE;
  pr_info("mmio_bench: Finished mmio benchmark\n");
    
  // print throughput
  pr_info("mmio_bench %llu : transfered %llu bytes in %llu cycles\n", size, tot_bytes, tot_elapsed);

  return 0;
}

// Function to map GPU memory
static int map_gpu_memory(void)
{
    struct mm_struct *mm;
    // Get the memory descriptor of the current process
    mm = current->mm;
    if (!mm) {
        printk(KERN_INFO "No memory descriptor found for the current process\n");
        return -EFAULT;
    }
    
    // Map MMIO memory region
    mmio_base = ioremap_wc(MMIO_BASE + MMIO_OFFSET, MMIO_SIZE);
    
    if (!mmio_base) {
        pr_err("mmio_bench: Failed to map MMIO memory\n");
        return -ENOMEM;
    }

    pr_info("mmio_bench: MMIO memory mapped at 0x%p\n", mmio_base);
    pr_info("mmio_bench: Page size %lu bytes\n", PAGE_SIZE);
    // print_cpu_clock_rate();

    mmio_bench_tput(bench_size);

    return 0;
}

// Function to unmap GPU memory
static void unmap_gpu_memory(void)
{
    if (mmio_base) {
        iounmap(mmio_base);
        pr_info("mmio_bench: MMIO memory unmapped\n");
    }
}

// Module initialization function
static int __init gpu_mem_init(void)
{
    int ret = map_gpu_memory();
    if (ret) {
        return ret;
    }

    pr_info("mmio_bench: GPU memory module loaded\n");
    return 0;
}

// Module exit function
static void __exit gpu_mem_exit(void)
{
    unmap_gpu_memory();
    pr_info("mmio_bench: GPU memory module unloaded\n");
}

module_init(gpu_mem_init);
module_exit(gpu_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ashfaqur Rahaman");
MODULE_DESCRIPTION("A simple Linux kernel module to map GPU memory and use MMIO");
MODULE_VERSION("1.0");
