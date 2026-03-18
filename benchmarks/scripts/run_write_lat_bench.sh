#!/bin/bash

# Run all client experiments for Figure-2 (Cost of DMA Ordering)
# Must be run on the client machine after the server is already running:
#   ./benchmarks/rdma/rdma_server 128 1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RDMA_DIR="$SCRIPT_DIR/../rdma"
RESULTS_DIR="$RDMA_DIR/results"

SERVER_IP="${SERVER_IP:-10.10.1.2}"
PORT="${PORT:-20079}"
CPU="${CPU:-0}"

mkdir -p "$RESULTS_DIR"

echo "=== Figure-2: Cost of DMA Ordering ==="
echo "Server: $SERVER_IP:$PORT"
echo "Results: $RESULTS_DIR"
echo ""

# Two ordered DMA writes
echo "[1/4] Two Ordered DMA..."
MLX5_POST_SEND_PREFER_BF=0 MLX5_SHUT_UP_BF=1 \
  taskset -c "$CPU" "$RDMA_DIR/write_lat" \
  -S "$SERVER_IP" -p "$PORT" -b 1 -t 1 -s 64 \
  -o "$RESULTS_DIR/ordered_dma.csv"
echo "Done."

sleep 5

# One DMA write
echo "[2/4] One DMA..."
MLX5_POST_SEND_PREFER_BF=1 MLX5_SHUT_UP_BF=0 \
  taskset -c "$CPU" "$RDMA_DIR/write_lat" \
  -S "$SERVER_IP" -p "$PORT" -b 1 -t 1 -s 64 \
  -o "$RESULTS_DIR/one_dma.csv"
echo "Done."

sleep 5

# Two unordered DMA writes
echo "[3/4] Two Unordered DMA..."
MLX5_POST_SEND_PREFER_BF=1 MLX5_SHUT_UP_BF=0 \
  taskset -c "$CPU" "$RDMA_DIR/write_bf_lat" \
  -S "$SERVER_IP" -p "$PORT" -b 1 -t 1 -s 64 \
  -o "$RESULTS_DIR/unordered_dma.csv"
echo "Done."

sleep 5

# All MMIO
echo "[4/4] All MMIO..."
MLX5_POST_SEND_PREFER_BF=1 MLX5_SHUT_UP_BF=0 \
  taskset -c "$CPU" "$RDMA_DIR/write_bf_inline_lat" \
  -S "$SERVER_IP" -p "$PORT" -b 1 -t 1 -s 32 \
  -o "$RESULTS_DIR/all_mmio.csv"
echo "Done."

sleep 5

echo ""
echo "=== Plotting results ==="
python3 "$SCRIPT_DIR/plot_rdma_write_lat.py"
echo "Plots saved to $RESULTS_DIR"
