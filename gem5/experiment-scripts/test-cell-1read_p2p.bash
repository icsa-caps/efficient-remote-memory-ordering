#!/bin/bash

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

declare -a cell_sizes=(64 128 256 512 1024 2048 4096 8192)
declare -a batch_sizes=(100 500)

# For debugging this script
# declare -a num_threads=(1 2)
# declare -a cell_sizes=(64 256)

TIMESTAMP=$(date +"%Y-%m-%d")

OUT_DIR=$RESULTS_DIR/cell-1r_p2p_$TIMESTAMP

if [ ! -d $OUT_DIR ]; then
    mkdir $OUT_DIR
fi

CUR_DIR=$(pwd)

cd ..

for batch_size in ${batch_sizes[@]}; do
    # Vary cell size
    for cell_size in ${cell_sizes[@]}; do
        # Run experiment for Speculative ordered reads without P2P reads
        build/X86/gem5.opt --outdir=$CUR_DIR/$OUT_DIR configs/pcie-experiments/rdma-cell-sim-speculative.py --num-cpus=1 --num-sockets=1 --cell-num-readers=1 --use-single-read --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro
        mv $CUR_DIR/$OUT_DIR/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t1-size$cell_size-batch$batch_size.txt
        # Run experiment for Speculative ordered reads with P2P reads
        build/X86/gem5.opt --outdir=$CUR_DIR/$OUT_DIR configs/pcie-experiments/rdma-cell-sim-speculative_p2p.py --num-cpus=1 --num-sockets=1 --cell-num-readers=1 --use-single-read --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro --switch-input-queue-size=32
        mv $CUR_DIR/$OUT_DIR/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t1-size$cell_size-batch$batch_size-p2p.txt
        # Run experiment for Speculative ordered reads with P2P reads with VOQs
        build/X86/gem5.opt --outdir=$CUR_DIR/$OUT_DIR configs/pcie-experiments/rdma-cell-sim-speculative_p2p.py --num-cpus=1 --num-sockets=1 --cell-num-readers=1 --use-single-read --cell-read-interval=1us --cell-read-batch=$batch_size --cell-exe=tests/test-progs/device-dma/bin/cell$cell_size-ro --use-switch-voq
        mv $CUR_DIR/$OUT_DIR/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t1-size$cell_size-batch$batch_size-p2p-voq.txt
    done
done

cd $CUR_DIR

