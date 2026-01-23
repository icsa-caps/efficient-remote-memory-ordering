#!/bin/bash

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

declare -a rdma_get_sizes=(64 128 256 512 1024 2048 4096 8192)

TIMESTAMP=$(date +"%Y-%m-%d")

OUT_DIR=$RESULTS_DIR/rd_stream_$TIMESTAMP

if [ ! -d $OUT_DIR ]; then
    mkdir $OUT_DIR
fi

CUR_DIR=$(pwd)

cd ..

for get_size in ${rdma_get_sizes[@]}; do
    # Block requests at NIC
    # build/X86/gem5.opt configs/pcie-experiments/rdma-sim-nic-block.py --num-cpus=1 --num-sockets=1 --block-qp --trc-file=tests/test-progs/device-dma/trc/read-dma-traces/read-test-size$get_size.txt
    # mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-block-requests-t1-size$get_size.txt
    # Block cache lines at NIC
    build/X86/gem5.opt configs/pcie-experiments/rdma-sim-nic-block.py --num-cpus=1 --num-sockets=1 --num-qps=1 --block-qp --use-cache-line-req --trc-file=tests/test-progs/device-dma/trc/read-dma-traces/read-test-size$get_size.txt
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-nic-block-cls-t1-size$get_size.txt
    # Block cache lines at RC
    build/X86/gem5.opt configs/pcie-experiments/rdma-sim-rc-block.py --num-cpus=1 --num-sockets=1 --num-qps=1 --trc-file=tests/test-progs/device-dma/trc/read-dma-traces/read-test-size$get_size.txt
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-rc-block-t1-size$get_size.txt
    # Speculative ordered
    build/X86/gem5.opt configs/pcie-experiments/rdma-sim-speculative.py --num-cpus=1 --num-sockets=1 --num-qps=1 --trc-file=tests/test-progs/device-dma/trc/read-dma-traces/read-test-size$get_size.txt
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-speculative-t1-size$get_size.txt
    # Unordered
    build/X86/gem5.opt configs/pcie-experiments/rdma-sim-unordered.py --num-cpus=1 --num-sockets=1 --num-qps=1 --trc-file=tests/test-progs/device-dma/trc/read-dma-traces/read-test-size$get_size.txt
    mv m5out/stats.txt $CUR_DIR/$OUT_DIR/stats-unordered-t1-size$get_size.txt
done

cd $CUR_DIR
