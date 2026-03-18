#!/bin/bash

# Single size
function check_variance() {
  size=64
  for i in $(seq 1 10); do
    sudo insmod mmio_bench.ko bench_size=$size
    sudo rmmod mmio_bench.ko
  done

  echo "Size: $size"
  echo "Cycles:"
  echo ""
  # FIXME: field numbers are machine dependent -- check dmesg for exact offset
  sudo dmesg | grep 'trans' | tail -n 9 | awk '{print $9}'
}

# Multiple sizes
function run_bench() {
  for sz in 64 128 256 512 1024 2048 4096 8192; do
    sudo insmod mmio_bench.ko bench_size=$sz
    sudo rmmod mmio_bench.ko
  done

  # print sizes
  echo "Sizes:"
  echo ""
  # FIXME: field numbers are machine dependent -- check dmesg for exact offset
  sudo dmesg | grep 'trans' | tail -n 9 | awk '{print $3}'
  echo ""
  echo "Cycles:"
  echo ""
  # FIXME: field numbers are machine dependent -- check dmesg for exact offset
  sudo dmesg | grep 'trans' | tail -n 9 | awk '{print $9}'
}

# check_variance
run_bench
