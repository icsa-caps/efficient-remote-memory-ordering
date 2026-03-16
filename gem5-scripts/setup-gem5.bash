#!/bin/bash

apt install build-essential scons python3-dev git pre-commit zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    libboost-all-dev  libhdf5-serial-dev python3-pydot python3-venv python3-tk mypy \
    m4 libcapstone-dev libpng-dev libelf-dev pkg-config wget cmake doxygen clang-format

CUR_DIR=$(pwd)

# Build gem5
cd ../gem5
scons build/X86/gem5.opt

# Make executables
cd tests/test-progs/device-dma

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

cd $CUR_DIR

