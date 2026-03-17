#!/bin/bash

CUR_DIR=$(pwd)

PYTHON=python3

# Make executables
cd ../gem5/tests/test-progs/device-dma

BIN_DIR=bin

if [ ! -d $BIN_DIR ]; then
    mkdir $BIN_DIR
fi

mkdir bin

make bin/inf-loop
make bin/idle-loop

make bin/cell64-ro
make bin/cell128-ro
make bin/cell256-ro
make bin/cell512-ro
make bin/cell1024-ro
make bin/cell2048-ro
make bin/cell4096-ro
make bin/cell8192-ro

make bin/mmio-write
make bin/mmio-write-mfence64
make bin/mmio-write-mfence128
make bin/mmio-write-mfence256
make bin/mmio-write-mfence512
make bin/mmio-write-mfence1024
make bin/mmio-write-mfence2048
make bin/mmio-write-mfence4096
make bin/mmio-write-mfence8192

make bin/inf-p2p

# Make DMA traces

TRC_DIR=trc

if [ ! -d $TRC_DIR ]; then
    mkdir $TRC_DIR
fi

mkdir trc
cd scripts
$PYTHON gen-validation-trace.py

cd $CUR_DIR

