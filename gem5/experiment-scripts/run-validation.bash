#!/bin/bash

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

declare -a cell_get_sizes=(64 128 256 512 1024 2048 4096 8192)

TIMESTAMP=$(date +"%Y-%m-%d")

OUT_DIR=$RESULTS_DIR/cell-val_$TIMESTAMP

if [ ! -d $OUT_DIR ]; then
    mkdir $OUT_DIR
fi

CUR_DIR=$(pwd)

cd ..

for cell_size in ${cell_get_sizes[@]}; do
    # Run experiment for double read gets
    build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --cell-num-readers=16 --cell-read-interval=1us --cell-read-batch=32 --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-double-t16-size$cell_size.txt
    # Run experiment for single read gets
    build/X86/gem5.opt configs/pcie-experiments/rdma-cell-sim-nic-block.py --num-cpus=1 --num-sockets=1 --use-single-read --cell-num-readers=16 --cell-read-interval=1us --cell-read-batch=32 --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-single-t16-size$cell_size.txt
done

cd $CUR_DIR


