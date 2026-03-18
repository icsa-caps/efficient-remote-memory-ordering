import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, FuncFormatter

PLOT_WIDTH = 3.38 # paper
PLOT_HEIGHT = 2 # paper

SMALL_SIZE = 9
MEDIUM_SIZE = 10
BIGGER_SIZE = 12
# font = font_manager.FontProperties(family='Times New Roman', size=16)
# font = {'family' : 'Times New Roman', 'size'   : 10, 'weight' : 'normal'}
# mpl.rc('font', **font)
plt.rc('font', size=SMALL_SIZE)          # controls default text sizes
plt.rc('axes', titlesize=SMALL_SIZE)     # fontsize of the axes title
plt.rc('axes', labelsize=MEDIUM_SIZE)    # fontsize of the x and y labels
plt.rc('xtick', labelsize=SMALL_SIZE)    # fontsize of the tick labels
plt.rc('ytick', labelsize=SMALL_SIZE)    # fontsize of the tick labels
plt.rc('legend', fontsize=SMALL_SIZE)    # legend fontsize
plt.rc('figure', titlesize=BIGGER_SIZE)

# Load CSV
df = pd.read_csv("results/write_lat.csv")
df_bf = pd.read_csv("results/write_bf_lat.csv")
df_bf_1cl = pd.read_csv("results/write_bf_lat_1cl.csv")
df_bf_inline = pd.read_csv("results/write_bf_inline_lat.csv")

# Extract latency column
latency = df["latency_ns"].values
latency_bf = df_bf["latency_ns"].values
latency_bf_1cl = df_bf_1cl["latency_ns"].values
latency_bf_inline = df_bf_inline["latency_ns"].values

# Sort values
latency_sorted = np.sort(latency)
latency_bf_sorted = np.sort(latency_bf)
latency_bf_1cl_sorted = np.sort(latency_bf_1cl)
latency_bf_inline_sorted = np.sort(latency_bf_inline)

# calculate 50th and 99th percentiles
p50 = np.percentile(latency, 50)
p99 = np.percentile(latency, 99)
p50_bf = np.percentile(latency_bf, 50)
p99_bf = np.percentile(latency_bf, 99)
p50_bf_1cl = np.percentile(latency_bf_1cl, 50)
p99_bf_1cl = np.percentile(latency_bf_1cl, 99)
p50_bf_inline = np.percentile(latency_bf_inline, 50)
p99_bf_inline = np.percentile(latency_bf_inline, 99)
print(f"Two Ordered DMA Reads: p50={p50} ns, p99={p99} ns")
print(f"Two Unordered DMA Reads: p50={p50_bf} ns, p99={p99_bf} ns")
print(f"One DMA Read: p50={p50_bf_1cl} ns, p99={p99_bf_1cl} ns")
print(f"All MMIO: p50={p50_bf_inline} ns, p99={p99_bf_inline} ns")

# Compute CDF
cdf = np.arange(1, len(latency_sorted) + 1) / len(latency_sorted)
cdf_bf = np.arange(1, len(latency_bf_sorted) + 1) / len(latency_bf_sorted)
cdf_bf_1cl = np.arange(1, len(latency_bf_1cl_sorted) + 1) / len(latency_bf_1cl_sorted)
cdf_bf_inline = np.arange(1, len(latency_bf_inline_sorted) + 1) / len(latency_bf_inline_sorted)

# Plot
fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
ax.plot(latency_sorted, cdf, label="Two Ordered DMA", color="#377eb8")
ax.plot(latency_bf_sorted, cdf_bf, label="Two Unordered DMA", color="#e41a1c")
ax.plot(latency_bf_1cl_sorted, cdf_bf_1cl, label="One DMA", color="#ff7f00")
ax.plot(latency_bf_inline_sorted, cdf_bf_inline, label="All MMIO", color="#4daf4a")

# max x value
# plt.xlim(0, max(latency_sorted.max(), latency_bf_sorted.max(), latency_bf_inline_sorted.max()) * 1.1)

# set x to 10000
ax.set_xlim(0, 5000)

# set x ticks every 100 ns
# plt.xticks(np.arange(0, 5001, 100))
ax.xaxis.set_major_locator(MultipleLocator(1000))
ax.xaxis.set_minor_locator(MultipleLocator(100))
# ax.set_xticks(np.arange(0, 5001, 100))

# ax.set_xticklabels([
#     str(v) if v % 500 == 0 else "" 
#     for v in np.arange(0, 5001, 500)
# ])
ax.xaxis.set_major_formatter(
    FuncFormatter(lambda v, pos: f"{int(v)}" if v % 1000 == 0 else "")
)
#
# start graph from 0,0
ax.set_ylim(0, 1)

ax.legend()
ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1.02), ncol=2)
ax.set_xlabel("Latency (ns)")
ax.set_ylabel("CDF")
# plt.grid(True)

# Save plot
fig.tight_layout()
fig.savefig("results/write_lat_cdf_final3.png")
fig.savefig("results/write_lat_cdf_final3.pdf")

