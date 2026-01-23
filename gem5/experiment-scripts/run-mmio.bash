#!/bin/bash

declare -a fence_intervals=(1 2 4 8 16 32 64 128)

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

TIMESTAMP=$(date +"%Y-%m-%d")

OUT_DIR=$RESULTS_DIR/mmio-wr-bench_$TIMESTAMP

if [ ! -d $OUT_DIR ]; then
    mkdir $OUT_DIR
fi

CUR_DIR=$(pwd)

cd ..

# Experiment without fence
build/X86/gem5.opt configs/pcie-experiments/mmio-write-sim.py --num-cpus=1 --num-sockets=1 > $CUR_DIR/$OUT_DIR/wc.txt 2>&1

if [[ -e $CUR_DIR/$OUT_DIR/wc-mfence.txt ]]; then
    rm $CUR_DIR/$OUT_DIR/wc-mfence.txt
fi

# Experiment with fence
for i in ${fence_intervals[@]}; do
    build/X86/gem5.opt configs/pcie-experiments/mmio-write-sim.py --num-cpus=1 --num-sockets=1 --simulate-fence --fence-interval=$i >> $CUR_DIR/$OUT_DIR/wc-mfence.txt 2>&1
done

cd $CUR_DIR
