BUILD_DIR = bin
SRC_DIR = src
BENCH_DIR = $(SRC_DIR)/benchmarks

$(BUILD_DIR)/bad-dev: $(SRC_DIR)/bad-device.c
	gcc -o $@ $^

$(BUILD_DIR)/dma-dev: $(SRC_DIR)/my-dma-device.c
	gcc -o $@ $^

$(BUILD_DIR)/inf-loop: $(SRC_DIR)/inf-loop.c
	gcc -o $@ $^

$(BUILD_DIR)/idle-loop: $(SRC_DIR)/idle-loop.c
	gcc -o $@ $^

$(BUILD_DIR)/write-loop: $(SRC_DIR)/write-loop.c
	gcc -o $@ $^

$(BUILD_DIR)/mem-read: $(SRC_DIR)/mem-read.c
	gcc -o $@ $^

$(BUILD_DIR)/cell: $(BENCH_DIR)/cell.c $(BENCH_DIR)/common.c $(BENCH_DIR)/dma.c $(BENCH_DIR)/rdwr.c
	gcc -DCREATE_WRITER -Wall -Wextra -Werror -O3 -std=c17 -o $@ $^

$(BUILD_DIR)/cell-ro: $(BENCH_DIR)/cell.c $(BENCH_DIR)/common.c $(BENCH_DIR)/dma.c $(BENCH_DIR)/rdwr.c
	gcc -Wall -Wextra -Werror -O3 -std=c17 -o $@ $^

$(BUILD_DIR)/mmio-write: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence64: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=64 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence128: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=128 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence256: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=256 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence512: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=512 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence1024: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=1024 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence2048: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=2048 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence4096: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=4096 -o $@ $^

$(BUILD_DIR)/mmio-write-mfence8192: $(BENCH_DIR)/mmio-write.c
	gcc -O3 -DUSE_MFENCE -DMFENCE_GRAN=8192 -o $@ $^

$(BUILD_DIR)/inf-p2p: $(SRC_DIR)/inf-p2p.c
	gcc -o $@ $^


