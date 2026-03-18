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
#define MMIO_BASE 0x98000000 // Replace with device MMIO base address
#define MMIO_SIZE 0x40000      // Size of MMIO region (l1tlb reach: 256KB)

#define FIXED_TOTAL_BYTES
#define TPUT_LOOP_SIZE 0x80000000 // Total bytes to transfer 2GB
#define TPUT_LOOP_ITER 128

#define MEMCPY 0
#define WRITEQ 0
#define AVX2 0
#define AVX512 0
#define MOVDIR64B 0
#define DRAM 1

// Memory access types for DRAM benchmark
#define DRAM_WB 0
#define DRAM_WC 1
#define DRAM_UC 0

// MOVDIR64B or AVX2 for DRAM benchmark
#define DRAM_MOVDIR64B 1

# define USE_SFENCE 0
// #define USE_MFENCE
// #define USE_RELAX_ORDER

#define WC 1

typedef struct
{
  uint64_t present                   :1;
  uint64_t writeable                 :1;
  uint64_t user_access               :1;
  uint64_t write_through             :1;
  uint64_t cache_disabled            :1;
  uint64_t accessed                  :1;
  uint64_t dirty                     :1;
  uint64_t size                      :1;
  uint64_t global                    :1;
  uint64_t ignored_2                 :3;
  uint64_t page_ppn                  :28;
  uint64_t reserved_1                :12; // must be 0
  uint64_t ignored_1                 :11;
  uint64_t execution_disabled        :1;
} __attribute__((__packed__)) PageTableEntry;

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

#if MEMCPY
    pr_info("mmio_bench: Running MEMCPY benchmark\n");
    // size = MMIO_SIZE;
    void *data = vmalloc(size);

    if (!data) {
      pr_err("mmio_bench %llu: Failed to allocate memory\n", size);
      return -ENOMEM;
    }

    memset(data, 37, size);

    start = mmio_rdtsc();
    x = 0;
    for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
      memcpy(mmio_base + x, data, size);
      x += size;
      if (x >= MMIO_SIZE)
        x = 0;
    }
    tot_elapsed = (mmio_rdtsc() - start);
    tot_bytes = TPUT_LOOP_SIZE;

    vfree(data);
#elif WRITEQ
    pr_info("mmio_bench: Running WRITEQ benchmark\n");
    if (size == 1) {
      for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
        *((volatile uint8_t *)mmio_base) = 37;
        tot_bytes += 1;
      }
    }
    else if (size == 2) {
      for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
        *((volatile uint16_t *)mmio_base) = 37;
        tot_bytes += 2;
      }
    }
    else if (size == 4) {
      for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
        *((volatile uint32_t *)mmio_base) = 37;
        tot_bytes += 4;
      }
    }
    else {
#ifdef FIXED_TOTAL_BYTES
      num_iters_i = TPUT_LOOP_SIZE / size;
      tot_bytes = TPUT_LOOP_SIZE;
#else
      num_iters_i = TPUT_LOOP_ITER;
      tot_bytes = TPUT_LOOP_ITER * size;
#endif
      num_iters_j = size / 8;
      x = 0;
      start = mmio_rdtsc();
      for (i = 0; i < num_iters_i; i+=2) {
        // start = mmio_rdtsc();
        for (j = 0; j < num_iters_j; j++) {
          // *((volatile uint64_t *)mmio_base + x + j) = 37;
#ifdef USE_RELAX_ORDER
          writeq_relaxed(37, mmio_base + x);
#else
          writeq(37, mmio_base + x);
#endif
          x += 8;
          if (x >= MMIO_SIZE)
            x = 0;
        }
        
        for (j = 0; j < num_iters_j; j++) {
          // *((volatile uint64_t *)mmio_base + x + j) = 37;
#ifdef USE_RELAX_ORDER
          writeq_relaxed(37, mmio_base + x);
#else
          writeq(37, mmio_base + x);
#endif
          x += 8;
          
          if (x >= MMIO_SIZE)
            x = 0;
        }
#ifdef USE_MFENCE
          mb();
#endif
        // tot_elapsed += (mmio_rdtsc() - start);
      }
      tot_elapsed = (mmio_rdtsc() - start);
    }
#elif AVX2
  pr_info("mmio_bench: Running AVX2 benchmark\n");
  if ((unsigned long)mmio_base % 32) {
    pr_err("mmio_bench: mmio_base is not 32-byte aligned\n");
    return -EINVAL;
  }
  /*
  void *data = vmalloc(size);

  if (!data) {
    pr_err("mmio_bench %llu: Failed to allocate memory\n", size);
    return -ENOMEM;
  }

  memset(data, 37, size);

  kernel_fpu_begin();
  asm volatile (
      "vmovdqa64 (%0), %%zmm0\n"  // Load 512-bit values into ZMM0 from `values`
      :
      : "r" (data)
      : "zmm0", "memory"          // Clobbered registers
  );
  kernel_fpu_end();
  */
  
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
  pr_info("mmio_bench: Finished AVX2 benchmark\n");
    
  // vfree(data);
#elif AVX512
  pr_info("mmio_bench: Running AVX512 benchmark\n");
  if ((unsigned long)mmio_base % 64) {
    pr_err("mmio_bench: mmio_base is not 64-byte aligned\n");
    return -EINVAL;
  }
  
  x = 0;
  kernel_fpu_begin();
  start = mmio_rdtsc();
  for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
    for (j = 0; j < size / 64; j++) {
      asm volatile (
          "vmovdqa64 %%zmm1, (%0)\n"  // Store ZMM0 into mmio_base
          :
          : "r" (mmio_base + x)  // Input operands
          : "memory"          // Clobbered registers
      );
      x += 64;
      if (x >= MMIO_SIZE)
        x = 0;
    }
  }
  tot_elapsed = (mmio_rdtsc() - start);
  kernel_fpu_end();
  tot_bytes = TPUT_LOOP_SIZE;
  pr_info("mmio_bench: Finished AVX512 benchmark\n");
#elif MOVDIR64B
  pr_info("mmio_bench: Running MOVDIR64B benchmark\n");
  
  if ((unsigned long)mmio_base % 64) {
    pr_err("mmio_bench: mmio_base is not 64-byte aligned\n");
    return -EINVAL;
  }
  
  void *data = vmalloc(MMIO_SIZE);

  if (!data) {
    pr_err("mmio_bench %llu: Failed to allocate memory\n", size);
    return -ENOMEM;
  }
  
  if ((unsigned long)data % 64) {
    pr_err("mmio_bench: Allocated memory is not 64-byte aligned\n");
    return -EINVAL;
  }

  memset(data, 37, MMIO_SIZE);
  
  x = 0;
  
  start = mmio_rdtsc();

  for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
    for (j = 0; j < size / 64; j++) {
      __asm__ __volatile__ (
          "movdir64b (%%rsi), %%rdi\n\t"  // Store ZMM0 into mmio_base
          :
          : "S" (data),
            "D" (mmio_base)
          : "memory"          // Clobbered registers
      );
      x += 64;
      if (x >= MMIO_SIZE)
        x = 0;
    }
  }

  tot_elapsed = (mmio_rdtsc() - start);
  tot_bytes = TPUT_LOOP_SIZE;
  pr_info("mmio_bench: Finished MOVDIR64B benchmark\n");
  vfree(data);
#elif DRAM
  void *ram_buf = vmalloc(MMIO_SIZE);
  
  if (!ram_buf) {
    pr_err("mmio_bench: Failed to allocate %d bytes memory\n", MMIO_SIZE);
    return -ENOMEM;
  }

#if DRAM_WC
  pr_info("mmio_bench: Running DRAM WC benchmark\n");
  if (set_memory_wc((unsigned long)ram_buf, MMIO_SIZE / PAGE_SIZE)) {
    printk(KERN_ERR "Failed to set memory to Write-Combining\n");
    vfree(ram_buf); // Free allocated memory on failure
    return -EFAULT;
  }
#elif DRAM_UC
  pr_info("mmio_bench: Running DRAM UC benchmark\n");
  if (set_memory_uc((unsigned long)ram_buf, MMIO_SIZE / PAGE_SIZE)) {
    printk(KERN_ERR "Failed to set memory to Uncacheable\n");
    vfree(ram_buf); // Free allocated memory on failure
    return -EFAULT;
  }
#else
  pr_info("mmio_bench: Running DRAM WB benchmark\n");
#endif
  
#if !DRAM_MOVDIR64B
  pr_info("mmio_bench: Running AVX2 DRAM benchmark\n");
  
  if ((unsigned long)ram_buf % 32) {
    pr_err("mmio_bench: %p is not 32-byte aligned\n", ram_buf);
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
          : "r" (ram_buf + x)  // Input operands
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
  pr_info("mmio_bench: Finished AVX2 DRAM benchmark\n");
#else
  pr_info("mmio_bench: Running MOVDIR64B DRAM benchmark\n");
  
  if ((unsigned long)ram_buf % 64) {
    pr_err("mmio_bench: mmio_base is not 64-byte aligned\n");
    return -EINVAL;
  }
  
  void *data = vmalloc(MMIO_SIZE);

  if (!data) {
    pr_err("mmio_bench %llu: Failed to allocate memory\n", size);
    return -ENOMEM;
  }
  
  if ((unsigned long)data % 64) {
    pr_err("mmio_bench: Allocated memory is not 64-byte aligned\n");
    return -EINVAL;
  }

  memset(data, 37, MMIO_SIZE);
  
  x = 0;
  
  start = mmio_rdtsc();

  for (i = 0; i < TPUT_LOOP_SIZE / size; i++) {
    for (j = 0; j < size / 64; j++) {
      __asm__ __volatile__ (
          "movdir64b (%%rsi), %%rdi\n\t"  // Store ZMM0 into mmio_base
          :
          : "S" (data),
            "D" (ram_buf + x)
          : "memory"          // Clobbered registers
      );
      x += 64;
      if (x >= MMIO_SIZE)
        x = 0;
    }
  }

  tot_elapsed = (mmio_rdtsc() - start);
  tot_bytes = TPUT_LOOP_SIZE;
  pr_info("mmio_bench: Finished MOVDIR64B DRAM benchmark\n");
  vfree(data);

#endif

#if !DRAM_WB
  if (set_memory_wb((unsigned long)ram_buf, MMIO_SIZE / PAGE_SIZE)) {
    printk(KERN_ERR "Failed to set memory to Write-Back\n");
    vfree(ram_buf); // Free allocated memory on failure
    return -EFAULT;
  }
#endif

  vfree(ram_buf);
#endif

  // print throughput
  pr_info("mmio_bench %llu : transfered %llu bytes in %llu cycles\n", size, tot_bytes, tot_elapsed);

  return 0;
}

static void print_pte_details(pte_t pte)
{
    PageTableEntry *entry = (PageTableEntry *)&pte;

    printk(KERN_INFO "Page Table Entry Details:\n");
    printk(KERN_INFO "  Present: %llu\n", entry->present);
    printk(KERN_INFO "  Writeable: %llu\n", entry->writeable);
    printk(KERN_INFO "  User Access: %llu\n", entry->user_access);
    printk(KERN_INFO "  Write Through: %llu\n", entry->write_through);
    printk(KERN_INFO "  Cache Disabled: %llu\n", entry->cache_disabled);
    printk(KERN_INFO "  Accessed: %llu\n", entry->accessed);
    printk(KERN_INFO "  Dirty: %llu\n", entry->dirty);
    printk(KERN_INFO "  PAT: %llu\n", entry->size);
    printk(KERN_INFO "  Global: %llu\n", entry->global);
    printk(KERN_INFO "  Page PPN: 0x%llx\n", entry->page_ppn);
    printk(KERN_INFO "  Execution Disabled: %llu\n", entry->execution_disabled);
}

static void dump_page_table(struct mm_struct *mm, unsigned long va)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    // Get the PGD
    pgd = pgd_offset(mm, va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk(KERN_INFO "Invalid PGD entry\n");
        return;
    }
    printk(KERN_INFO "PGD entry: 0x%lx\n", pgd_val(*pgd));
    
    p4d = p4d_offset(pgd, va);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk(KERN_INFO "Invalid P4D entry\n");
        return;
    }
    printk(KERN_INFO "P4D entry: 0x%lx\n", p4d_val(*p4d));

    // Get the PUD
    pud = pud_offset(p4d, va);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk(KERN_INFO "Invalid PUD entry\n");
        return;
    }
    printk(KERN_INFO "PUD entry: 0x%lx\n", pud_val(*pud));

    // Get the PMD
    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        printk(KERN_INFO "Invalid PMD entry\n");
        return;
    }
    printk(KERN_INFO "PMD entry: 0x%lx\n", pmd_val(*pmd));

    // Get the PTE
    pte = pte_offset_kernel(pmd, va);
    if (!pte) {
        printk(KERN_INFO "No PTE found\n");
        return;
    }
    printk(KERN_INFO "PTE entry: 0x%lx\n", pte_val(*pte));

    print_pte_details(*pte);
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
#if WC
    mmio_base = ioremap_wc(MMIO_BASE, MMIO_SIZE);
#else
    mmio_base = ioremap(MMIO_BASE, MMIO_SIZE);
#endif
    
    if (!mmio_base) {
        pr_err("mmio_bench: Failed to map MMIO memory\n");
        return -ENOMEM;
    }

    pr_info("mmio_bench: MMIO memory mapped at 0x%p\n", mmio_base);
    pr_info("mmio_bench: Page size %lu bytes\n", PAGE_SIZE);
    // print_cpu_clock_rate();

    mmio_bench_tput(bench_size);
    // dump_page_table(mm, mmio_base);

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
