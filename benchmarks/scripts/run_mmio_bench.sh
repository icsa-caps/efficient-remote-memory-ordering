#!/bin/bash

function run_bench() {
  local use_sfence=$1
  local out=$2
  local mmio_dir="$(dirname "$0")/../mmio"

  echo "Building mmio_bench.ko with USE_SFENCE=$use_sfence..."
  make -C "$mmio_dir" clean
  make -C "$mmio_dir" USE_SFENCE=$use_sfence
  if [ $? -ne 0 ]; then
    echo "Build failed for USE_SFENCE=$use_sfence" >&2
    exit 1
  fi

  for sz in 64 128 256 512 1024 2048 4096 8192; do
    sudo insmod "$mmio_dir/mmio_bench.ko" bench_size=$sz
    sudo rmmod mmio_bench
  done

  # Get average CPU frequency in Hz from /proc/cpuinfo
  freq_hz=$(awk '/cpu MHz/ {sum += $4; count++} END {print sum/count * 1e6}' /proc/cpuinfo)

  # FIXME: field numbers are machine dependent -- check dmesg for exact offset
  sudo dmesg | grep 'trans' | tail -n 8 | awk -v freq="$freq_hz" -v sfence="$use_sfence" '{
    size = $3
    cycles = $9
    gbits = (2 * 1024 * 1024 * 1024 * 8 * freq) / (cycles * 1e9)
    printf "%s,%s,%.2f,%s\n", size, cycles, gbits, sfence
  }' >> "$out"
}

results_dir="$(dirname "$0")/../mmio/results"
mkdir -p "$results_dir"
out="$results_dir/mmio_bench.csv"

echo "size_b,cycles,gbps,sfence" > "$out"
run_bench 0 "$out"
run_bench 1 "$out"

echo "Results saved to $out"

script_dir="$(dirname "$0")"
python3 "$script_dir/plot_mmio_bench.py" "$out" "$results_dir"
echo "Plots saved to $results_dir"
