#!/bin/bash

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

declare -a num_threads=(1 2 4 8 16)
declare -a cell_sizes=(64 128 512 1024 2048 4096 8192)
declare -a batch_sizes=(100 500)

# For debugging this script
# declare -a num_threads=(1 2)
# declare -a cell_sizes=(64 256)

TIMESTAMP=$(date +"%Y-%m-%d")

OUT_DIR=$RESULTS_DIR/cell-2r_$TIMESTAMP

if [ ! -d $OUT_DIR ]; then
    mkdir $OUT_DIR
fi

CUR_DIR=$(pwd)

cd ..
batch_size in ${batch_sizes[@]}

for batch_size in ${batch_sizes[@]}; do
    # Vary cell size
    for cell_size in ${cell_sizes[@]}; do
        # Run experiment for blocking requests at the NIC
        # build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --cell-num-readers=$t --cell-read-interval=1us --cell-read-batch=32 --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        # mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-block-t$t-size$cell_size.txt
        # Run experiment for blocking cache lines at the NIC
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --use-cache-line-req --cell-num-readers=1 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-cl-t1-size$cell_size-batch$batch_size.txt
        # Run experiment for RC order
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-rc-block.py --num-cpus=1 --num-sockets=1 --cell-num-readers=1 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-rc-block-t1-size$cell_size-batch$batch_size.txt
        # Run experiment for Speculative ordered reads
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-speculative.py --num-cpus=1 --num-sockets=1 --cell-num-readers=1 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t1-size$cell_size-batch$batch_size.txt
        # 16 QPs
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --use-cache-line-req --cell-num-readers=16 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-cl-t16-size$cell_size-batch$batch_size.txt
        # Run experiment for RC order
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-rc-block.py --num-cpus=1 --num-sockets=1 --cell-num-readers=16 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-rc-block-t16-size$cell_size-batch$batch_size.txt
        # Run experiment for Speculative ordered reads
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-speculative.py --num-cpus=1 --num-sockets=1 --cell-num-readers=16 --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t16-size$cell_size-batch$batch_size.txt
    done

    # Vary num queue pairs
    for t in ${num_threads[@]}; do
        # Run experiment for blocking cache lines at the NIC
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --use-cache-line-req --cell-num-readers=$t --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell256-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-cl-t$t-size256-batch$batch_size.txt
        # Run experiment for RC order
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-rc-block.py --num-cpus=1 --num-sockets=1 --cell-num-readers=$t --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell256-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-rc-block-t$t-size256-batch$batch_size.txt
        # Run experiment for Speculative ordered reads
        build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-speculative.py --num-cpus=1 --num-sockets=1 --cell-num-readers=$t --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell256-ro
        mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t$t-size256-batch$batch_size.txt
    done
done

cd $CUR_DIR

