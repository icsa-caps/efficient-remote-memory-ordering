#!/bin/bash

LOG_FILE="benchmark_results.log"
TH_SCALE_CSV="results/qp_th_scale_r6525.csv"
NUM_RUNS=5
NUM_THREADS=16
NUMA_NODE=0
  
echo "Benchmark, QP, Threads, Batch, Throughput (Gbps), Latency (us)" > $TH_SCALE_CSV

num_qps=(1 2)

# read benchmark
for qp in ${num_qps[@]}; do
  for i in $(seq 1 $NUM_THREADS); do
    echo "Running READ benchmark with QP: $qp, Threads: $i"
    total_tpu=0
    total_lat=0
    for n in $(seq 1 $NUM_RUNS); do
      # batch_size=$((32 * qp))
      # batch_size=$((8192 / $i))
      batch_size=32
      numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} ../rdma/read_bench --batch_size $batch_size --object_size 64 -q $qp -t $i > $LOG_FILE
      tput=$(cat $LOG_FILE | grep "Gbps" | awk '{print $2}')
      lat=$(cat $LOG_FILE | grep "Avg." | awk '{print $3}')
      total_tpu=$(echo "$total_tpu + $tput" | bc)
      total_lat=$(echo "$total_lat + $lat" | bc)
    done
    res_tput=$(echo "scale=3; $total_tpu / $NUM_RUNS" | bc)
    res_lat=$(echo "scale=3; $total_lat / $NUM_RUNS" | bc)
    echo "READ, $qp, $i, $batch_size, $res_tput, $res_lat" | tee -a $TH_SCALE_CSV
  done
done

# write benchmark
for qp in ${num_qps[@]}; do
  for i in $(seq 1 $NUM_THREADS); do
    echo "Running WRITE benchmark with QP: $qp, Threads: $i"
    total_tpu=0
    total_lat=0
    for n in $(seq 1 $NUM_RUNS); do
      batch_size=$((32 * qp))
      # batch_size=$((8192 / $i))
      batch_size=32
      numactl --cpunodebind=${NUMA_NODE} --membind=${NUMA_NODE} ../rdma/write_bench --batch_size $batch_size --object_size 64 -q $qp -t $i > $LOG_FILE
      tput=$(cat $LOG_FILE | grep "Gbps" | awk '{print $2}')
      lat=$(cat $LOG_FILE | grep "Avg." | awk '{print $3}')
      total_tpu=$(echo "$total_tpu + $tput" | bc)
      total_lat=$(echo "$total_lat + $lat" | bc)
    done
    res_tput=$(echo "scale=3; $total_tpu / $NUM_RUNS" | bc)
    res_lat=$(echo "scale=3; $total_lat / $NUM_RUNS" | bc)
    echo "WRITE, $qp, $i, $batch_size, $res_tput, $res_lat" | tee -a $TH_SCALE_CSV
  done
done

# fence benchmark
# for qp in 1 2 4 8 16; do
#   for i in $(seq 1 $NUM_THREADS); do
#     echo "Running FENCE benchmark with QP: $qp, Threads: $i"
#     batch_size=$((32 * qp))
#     # batch_size=32
#     numactl --physcpubind=32-40 --membind=1 ./read_bench --batch_size $batch_size --object_size 64 -q $qp -t $i -f > $LOG_FILE
#     res_tput=$(cat $LOG_FILE | grep "Gbps" | awk '{print $2}')
#     res_lat=$(cat $LOG_FILE | grep "Avg." | awk '{print $3}')
#     echo "FENCE, $qp, $i, $batch_size, $res_tput, $res_lat" | tee -a $TH_SCALE_CSV
#   done
# done
