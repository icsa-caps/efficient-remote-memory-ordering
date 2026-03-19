#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RDMA_DIR="$SCRIPT_DIR/../rdma"

RESULTS_DIR="$RDMA_DIR/results"
mkdir -p "$RESULTS_DIR"

LOG_FILE="$RESULTS_DIR/benchmark_results.log"
TH_SCALE_CSV="$RESULTS_DIR/rdma_read_write_bench.csv"
NUM_RUNS=10
NUM_THREADS=16
NUMA_NODE=0

SERVER_IP="${SERVER_IP:-10.10.1.1}"
PORT="${PORT:-20079}"
  
echo "Benchmark, QP, Threads, Batch, Throughput (Gbps), Throughput (ops/sec), Latency (us)" > $TH_SCALE_CSV

num_qps=(1 2)

# read benchmark
for qp in ${num_qps[@]}; do
  for i in $(seq 1 $NUM_THREADS); do
    echo "Running READ benchmark with QP: $qp, Threads: $i"
    total_tpu=0
    total_tput_ops=0
    total_lat=0
    for n in $(seq 1 $NUM_RUNS); do
      # batch_size=$((32 * qp))
      # batch_size=$((8192 / $i))
      batch_size=32
      numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} "$RDMA_DIR/read_bench" --batch_size $batch_size --object_size 64 -q $qp -t $i -S "$SERVER_IP" -p "$PORT" > $LOG_FILE
      tput=$(cat $LOG_FILE | grep "Gbps" | awk '{print $2}')
      tput_ops=$(cat $LOG_FILE | grep "ops/sec" | awk '{print $2}')
      lat=$(cat $LOG_FILE | grep "Avg." | awk '{print $3}')
      total_tpu=$(echo "$total_tpu + $tput" | bc)
      total_tput_ops=$(echo "$total_tput_ops + $tput_ops" | bc)
      total_lat=$(echo "$total_lat + $lat" | bc)
    done
    res_tput=$(echo "scale=3; $total_tpu / $NUM_RUNS" | bc)
    res_tput_ops=$(echo "scale=3; $total_tput_ops / $NUM_RUNS" | bc)
    res_lat=$(echo "scale=3; $total_lat / $NUM_RUNS" | bc)
    echo "READ, $qp, $i, $batch_size, $res_tput, $res_tput_ops, $res_lat" | tee -a $TH_SCALE_CSV
  done
done

# write benchmark
for qp in ${num_qps[@]}; do
  for i in $(seq 1 $NUM_THREADS); do
    echo "Running WRITE benchmark with QP: $qp, Threads: $i"
    total_tpu=0
    total_tput_ops=0
    total_lat=0
    for n in $(seq 1 $NUM_RUNS); do
      batch_size=$((32 * qp))
      # batch_size=$((8192 / $i))
      batch_size=32
      numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} "$RDMA_DIR/write_bench" --batch_size $batch_size --object_size 64 -q $qp -t $i -S "$SERVER_IP" -p "$PORT" > $LOG_FILE
      tput=$(cat $LOG_FILE | grep "Gbps" | awk '{print $2}')
      tput_ops=$(cat $LOG_FILE | grep "ops/sec" | awk '{print $2}')
      lat=$(cat $LOG_FILE | grep "Avg." | awk '{print $3}')
      total_tpu=$(echo "$total_tpu + $tput" | bc)
      total_tput_ops=$(echo "$total_tput_ops + $tput_ops" | bc)
      total_lat=$(echo "$total_lat + $lat" | bc)
    done
    res_tput=$(echo "scale=3; $total_tpu / $NUM_RUNS" | bc)
    res_tput_ops=$(echo "scale=3; $total_tput_ops / $NUM_RUNS" | bc)
    res_lat=$(echo "scale=3; $total_lat / $NUM_RUNS" | bc)
    echo "WRITE, $qp, $i, $batch_size, $res_tput, $res_tput_ops, $res_lat" | tee -a $TH_SCALE_CSV
  done
done

echo ""
echo "=== Plotting results ==="
python3 "$SCRIPT_DIR/plot_rdma_bench.py" "$TH_SCALE_CSV" --output_dir "$RESULTS_DIR" --type bar
echo "Plots saved to $RESULTS_DIR"
