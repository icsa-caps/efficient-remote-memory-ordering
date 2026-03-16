import matplotlib.pyplot as plt
# import matplotlib.font_manager as font_manager
# import matplotlib.ticker as ticker
# import matplotlib as mpl
# import os
import numpy as np
# import pandas as pd
from matplotlib.ticker import ScalarFormatter
# from math import log2
from sys import argv

PLOT_WIDTH = 3.38
PLOT_HEIGHT = 2.8

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

def get_value_from_file(stat_file, key):
    for line in stat_file:
        if key in line:
            first_space_idx = line.find(' ')
            tmp = (line[first_space_idx:]).strip()
            value = float((tmp.split(' '))[0])
            return value
        
    print("Value not found for key: " + key)
    return None

# Returns time interval between ticks in seconds
def get_tick_interval(stat_file):
    freq = get_value_from_file(stat_file, "simFreq")
    return (1/freq)

# For Gigabytes per second, set order = 9
def get_throughput(stat_file, order=9):
    data_Bs = get_value_from_file(stat_file, "system.dma_device.avgDmaThroughput")
    return data_Bs / (10**order)

def get_cell_mops(stat_file):
    data_mops = get_value_from_file(stat_file, "system.dma_device.avgCellGetMops")
    return data_mops

def get_cell_cpu_mops(stat_file):
    data_mops = get_value_from_file(stat_file, "system.dma_device.avgCpuCellGetMops")
    return data_mops

def get_cell_cpu_tput_gbps(stat_file):
    data_Bs = get_value_from_file(stat_file, "system.dma_device.cpuCellRdThroughput")
    return ((data_Bs * 8) / (1024**3))

def get_cell_p2p_tput_gbps(stat_file):
    data_Bs = get_value_from_file(stat_file, "system.dma_device.p2pCellRdThroughput")
    return ((data_Bs * 8) / (1024**3))

def plot_dma_threads_size(dname, size, nthread):
    fig,ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    xtick_arr = None
    xaxis_label = None

    if (size == 0):
        # Vary cell size
        sizes = [1 << i for i in range(6,14)]
        cl_block_tput = []
        rc_block_tput = []
        speculative_tput = []
        unordered_tput = []
        for s in sizes:
            with open("results/" + dname + "stats-nic-block-cls-t" + str(nthread) + "-size" + str(s) + ".txt", 'r') as clfile:
                cl_block_tput.append(get_throughput(clfile))
            with open("results/" + dname + "stats-rc-block-t" + str(nthread) + "-size" + str(s) + ".txt", 'r') as rcfile:
                rc_block_tput.append(get_throughput(rcfile))
            with open("results/" + dname + "stats-speculative-t" + str(nthread) + "-size" + str(s) + ".txt", 'r') as spfile:
                speculative_tput.append(get_throughput(spfile))
            with open("results/" + dname + "stats-unordered-t" + str(nthread) + "-size" + str(s) + ".txt", 'r') as uofile:
                unordered_tput.append(get_throughput(uofile))

        ax.plot(sizes, cl_block_tput, label="NIC", marker='x', color="#e41a1c")
        ax.plot(sizes, rc_block_tput, label="RC", marker='.', color="#984ea3")
        ax.plot(sizes, speculative_tput, label="RC-opt", marker='*', markersize=12, linewidth=3, color="#377eb8")
        ax.plot(sizes, unordered_tput, label="Unordered", marker='p', color="#4daf4a")
        xtick_arr = sizes
        xlabel_arr = ['64', '128', '256', '512', '1K', '2K', '4K', '8K']
        xaxis_label = "DMA Read Size (B)"
        # plot_title = "Cell Read Throughput with batch size " + str(batch)
        save_name = "rd-stream_tput_size_t" + str(nthread) + ".pdf"
        # plt.legend(loc="upper right")

    else:
        # vary number of threads
        nthreads = [1 << i for i in range(5)]
        # nic_block_mops = []
        cl_block_tput = []
        rc_block_tput = []
        speculative_tput = []
        unordered_tput = []
        for t in nthreads:
            with open("results/" + dname + "stats-nic-block-cls-t" + str(t) + "-size" + str(size) + ".txt", 'r') as clfile:
                cl_block_tput.append(get_throughput(clfile))
            with open("results/" + dname + "stats-rc-block-t" + str(t) + "-size" + str(size) + ".txt", 'r') as rcfile:
                rc_block_tput.append(get_throughput(rcfile))
            with open("results/" + dname + "stats-speculative-t" + str(t) + "-size" + str(size) + ".txt", 'r') as spfile:
                speculative_tput.append(get_throughput(spfile))
            with open("results/" + dname + "stats-unordered-t" + str(t) + "-size" + str(size) + ".txt", 'r') as uofile:
                unordered_tput.append(get_throughput(uofile))
        
        ax.plot(sizes, cl_block_tput, label="NIC", marker='x', color="#e41a1c")
        ax.plot(sizes, rc_block_tput, label="RC", marker='.', color="#984ea3")
        ax.plot(sizes, speculative_tput, label="RC-opt", marker='*', markersize=12, linewidth=3,color="#377eb8")
        ax.plot(sizes, unordered_tput, label="Unordered", marker='p', color="#4daf4a")
        xtick_arr = nthreads
        xlabel_arr = nthreads
        xaxis_label = "Number of queue pairs"
        # plot_title = "Cell Read Throughput with batch size " + str(batch)
        save_name = argv[1] + "_rd-stream_tput_size_t" + str(size) + ".pdf"
        # plt.legend(loc="lower right")

    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.3), ncol=2)

    plt.xscale("log")
    ax.xaxis.set_major_formatter(ScalarFormatter())
    plt.xticks(ticks=xtick_arr, labels=xlabel_arr)
    # ax.xaxis.minorticks_off()
    ax.minorticks_off()

    ax.set_ylim(bottom=0)

    plt.xlabel(xaxis_label)
    plt.ylabel("Throughput (GB/s)")

    fig = plt.gcf()
    fig.tight_layout()

    # plt.title(plot_title)
    fig.savefig("plots/" + save_name)

def plot_cell_dma_threads_size(exp_name, dname, size, nthread, batch):
    fig,ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    xtick_arr = None
    xaxis_label = None

    if (size == 0):
        # Vary cell size
        sizes = [1 << i for i in range(6,14)]
        # nic_block_mops = []
        cl_block_mops = []
        rc_block_mops = []
        speculative_mops = []
        for s in sizes:
            # with open("results/" + dname + "stats-nic-block-t" + str(nthread) + "-size" + str(s) + ".txt", 'r') as nbfile:
            #     nic_block_mops.append(get_cell_mops(nbfile))
            with open("results/" + dname + "stats-nic-cl-t" + str(nthread) + "-size" + str(s) + "-batch" + str(batch) + ".txt", 'r') as clfile:
                cl_block_mops.append(get_cell_mops(clfile))
            with open("results/" + dname + "stats-rc-block-t" + str(nthread) + "-size" + str(s) + "-batch" + str(batch) + ".txt", 'r') as rcfile:
                rc_block_mops.append(get_cell_mops(rcfile))
            with open("results/" + dname + "stats-speculative-t" + str(nthread) + "-size" + str(s) + "-batch" + str(batch) + ".txt", 'r') as spfile:
                speculative_mops.append(get_cell_mops(spfile))

        # ax.plot(sizes, nic_block_mops, label="cell", marker='x')
        ax.plot(sizes, cl_block_mops, label="NIC", marker='x', color="#e41a1c")
        ax.plot(sizes, rc_block_mops, label="RC", marker='.', color="#984ea3")
        ax.plot(sizes, speculative_mops, label="RC-opt", marker='*', color="#377eb8")
        xtick_arr = sizes
        xlabel_arr = ['64', '128', '256', '512', '1K', '2K', '4K', '8K']
        xaxis_label = "Object Size (B)"
        # plot_title = "Cell Read Throughput with batch size " + str(batch)
        save_name = exp_name + "_tput_size_t" + str(nthread) + "_batch" + str(batch) + ".pdf"
        # plt.legend(loc="upper right")

    else:
        # vary number of threads
        nthreads = [1 << i for i in range(5)]
        # nic_block_mops = []
        cl_block_mops = []
        rc_block_mops = []
        speculative_mops = []
        for t in nthreads:
            # with open("results/" + dname + "stats-nic-block-t" + str(t) + "-size" + str(size) + ".txt", 'r') as nbfile:
            #     nic_block_mops.append(get_cell_mops(nbfile))
            with open("results/" + dname + "stats-nic-cl-t" + str(t) + "-size" + str(size) + "-batch" + str(batch) + ".txt", 'r') as clfile:
                cl_block_mops.append(get_cell_mops(clfile))
            with open("results/" + dname + "stats-rc-block-t" + str(t) + "-size" + str(size) + "-batch" + str(batch) + ".txt", 'r') as rcfile:
                rc_block_mops.append(get_cell_mops(rcfile))
            with open("results/" + dname + "stats-speculative-t" + str(t) + "-size" + str(size) + "-batch" + str(batch) + ".txt", 'r') as spfile:
                speculative_mops.append(get_cell_mops(spfile))
        
        # ax.plot(nthreads, nic_block_mops, label="NIC Block Requests", marker='x')
        ax.plot(nthreads, cl_block_mops, label="NIC", marker='x', color="#e41a1c")
        ax.plot(nthreads, rc_block_mops, label="RC", marker='.', color="#984ea3")
        ax.plot(nthreads, speculative_mops, label="RC-opt", marker='*', color="#377eb8")
        xtick_arr = nthreads
        xlabel_arr = nthreads
        xaxis_label = "Number of queue pairs"
        # plot_title = "Cell Read Throughput with batch size " + str(batch)
        save_name = exp_name +  "_tput_threads_s" + str(size) + "_batch" + str(batch) + ".pdf"
        # plt.legend(loc="lower right")

    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.15), ncol=3)

    plt.xscale("log")
    ax.xaxis.set_major_formatter(ScalarFormatter())
    plt.xticks(ticks=xtick_arr, labels=xlabel_arr)
    # ax.xaxis.minorticks_off()
    ax.minorticks_off()

    ax.set_ylim(bottom=0)

    plt.xlabel(xaxis_label)
    plt.ylabel("Throughput (M GET/s)")

    fig = plt.gcf()
    fig.tight_layout()

    # plt.title(plot_title)
    fig.savefig("plots/" + save_name)

def plot_cell_validation(dname):
    fig,ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    xtick_arr = [1 << i for i in range(6,14)]

    single_mgets = []
    double_mgets = []
    for size in xtick_arr:
        with open("results/" + dname + "stats-double-t16" + "-size" + str(size) + ".txt", 'r') as dfile:
            double_mgets.append(get_cell_mops(dfile))
        with open("results/" + dname + "stats-single-t16" + "-size" + str(size) + ".txt", 'r') as sfile:
            single_mgets.append(get_cell_mops(sfile))
    
    ax.plot(xtick_arr, double_mgets, label="Validation", marker='.', color="#984ea3")
    ax.plot(xtick_arr, single_mgets, label="Single Read", marker='*', color="#377eb8")

    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.15), ncol=2)

    plt.xscale("log")
    ax.xaxis.set_major_formatter(ScalarFormatter())
    plt.xticks(ticks=xtick_arr, labels=['64', '128', '256', '512', '1K', '2K', '4K', '8K'])
    # ax.xaxis.minorticks_off()
    ax.minorticks_off()

    ax.set_ylim(bottom=0)

    plt.xlabel("Object Size (B)")
    plt.ylabel("Throughput (M GET/s)")

    fig = plt.gcf()
    fig.tight_layout()

    # plt.title(plot_title)
    fig.savefig("plots/cell-validation.pdf")

def read_mmio_write_bench(fname):
    trf_size = []
    total_size = []
    cycles = []
    with open(fname, 'r') as result_file:
        for line in result_file:
            if "transfered" in line:
                txt_arr = line.split()
                trf_size.append(int(txt_arr[1]))
                total_size.append(int(txt_arr[4]))
                cycles.append(int(txt_arr[7]))
    return trf_size, total_size, cycles

def plot_mmio_fence_tput(dname):
    labels = ["MMIO", "MMIO + fence"]
    fnames = ["wc.txt", "wc-mfence.txt"]
    markers = ['*', 'x']
    colors = ["#377eb8", "#e41a1c", "#4daf4a", "#984ea3"]
    trf_sizes = []

    fig,ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    index = 0
    for fname in fnames:
        trf_sizes, total_size, cycles = read_mmio_write_bench("results/" + dname + fname)
        # Throughput in Gb/s assuming simulation frequency of 3GHz
        tputs = [((total_size[i] / cycles[i]) * 3 * 8) for i in range(len(trf_sizes))]
        ax.plot(trf_sizes, tputs, label=labels[index], marker=markers[index], color=colors[index])
        index += 1

    ax.plot(trf_sizes, [100 for i in trf_sizes], label="NIC B/W Limit", color="#000000", linestyle="--")
    # plt.legend(loc="lower right")
    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.3), ncol=2)

    plt.xscale("log")
    ax.xaxis.set_major_formatter(ScalarFormatter())
    plt.xticks(ticks=trf_sizes, labels=['64', '128', '256', '512', '1K', '2K', '4K', '8K'])
    # ax.xaxis.minorticks_off()
    ax.minorticks_off()

    ax.set_ylim(bottom=0)

    plt.xlabel("Message Size (B)")
    plt.ylabel("Throughput (Gb/s)")

    fig = plt.gcf()
    fig.tight_layout()
    
    # plt.title("Plot of MMIO Write Throughput")
    fig.savefig("plots/mmio-write-throughput.pdf", dpi=300)

def plot_p2p_tput(dname, batch):
    fig,ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    xtick_arr = None
    xaxis_label = None

    # Vary cell size
    sizes = [1 << i for i in range(6,14)]
    speculative_mops = []
    voq_cpu_mops = []
    voq_p2p_mops = []
    nov_cpu_mops = []
    nov_p2p_mops = []

    for s in sizes:
        with open("results/" + dname + "stats-speculative-t1" + "-size" + str(s) + "-batch" + str(batch) + ".txt", 'r') as spfile:
            # speculative_mops.append(get_cell_mops(spfile))
            speculative_mops.append(get_cell_cpu_tput_gbps(spfile))
        with open("results/" + dname + "stats-speculative-t1" + "-size" + str(s) + "-batch" + str(batch) + "-p2p-voq.txt", 'r') as vqfile:
            # tot_mops = get_cell_mops(vqfile)
            # cpu_mops = get_cell_cpu_mops(vqfile)
            # voq_cpu_mops.append(cpu_mops)
            # voq_p2p_mops.append(tot_mops - cpu_mops)
            voq_cpu_mops.append(get_cell_cpu_tput_gbps(vqfile))
            voq_p2p_mops.append(get_cell_p2p_tput_gbps(vqfile))
        with open("results/" + dname + "stats-speculative-t1" + "-size" + str(s) + "-batch" + str(batch) + "-p2p.txt", 'r') as ppfile:
            # tot_mops = get_cell_mops(ppfile)
            # cpu_mops = get_cell_cpu_mops(ppfile)
            # nov_p2p_mops.append(cpu_mops)
            # nov_cpu_mops.append(tot_mops - cpu_mops)
            nov_cpu_mops.append(get_cell_cpu_tput_gbps(ppfile))
            nov_p2p_mops.append(get_cell_p2p_tput_gbps(ppfile))

    np_sizes = np.array(sizes)
    widths = np.array([10 << i for i in range(0,len(sizes))])
    # Baseline speculative bar
    ax.bar(np_sizes - widths, speculative_mops, width=widths, label="Reads to CPU, no P2P transfers", color="#377eb8")
    # With VOQs
    zero_arr = np.zeros(len(sizes))
    ax.bar(np_sizes, voq_cpu_mops, width=widths, bottom=zero_arr, label="Reads to CPU, P2P transfers", color="#984ea3")
    ax.bar(np_sizes, voq_p2p_mops, width=widths, bottom=zero_arr+np.array(voq_cpu_mops), label="Reads to P2P device", color="#e41a1c")
    # P2P without VOQs
    ax.bar(np_sizes + widths, nov_cpu_mops, width=widths, bottom=zero_arr, color="#984ea3")
    ax.bar(np_sizes + widths, nov_p2p_mops, width=widths, bottom=zero_arr+np.array(nov_cpu_mops), color="#e41a1c")

    # bar_handles, bar_labels = ax.get_legend_handles_labels()

    # line0 = ax.plot(np_sizes - widths, speculative_mops, label="Total, no P2P transfers", marker='*', markersize=4, color='k')
    # line1 = ax.plot(np_sizes, np.array(voq_cpu_mops)+np.array(voq_p2p_mops), label="Total, VOQ switch", marker='.', markersize=4, color='k')
    # line2 = ax.plot(np_sizes + widths, np.array(nov_cpu_mops)+np.array(nov_p2p_mops), label="Overall b/w without VOQs", marker='.', color='k')

    xtick_arr = sizes
    xlabel_arr = ['64', '128', '256', '512', '1K', '2K', '4K', '8K']
    xaxis_label = "Object Size (B)"
    # plot_title = "Cell Read Throughput with P2P Traffic"
    save_name = "cell-1r-p2p_tput_size_t1_batch" + str(batch) + ".pdf"

    # bar_leg = ax.legend(handles=bar_handles, labels=bar_labels, loc='upper center', bbox_to_anchor=(0.65, 1.5), ncol=1)
    # ax.add_artist(bar_leg)
    # ax.legend(handles=[line0[0], line1[0]], loc='upper center', bbox_to_anchor=(0.1, 1.5), ncol=1)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=1)

    plt.xscale("log")
    ax.xaxis.set_major_formatter(ScalarFormatter())
    plt.xticks(ticks=xtick_arr, labels=xlabel_arr)
    # ax.xaxis.minorticks_off()
    ax.minorticks_off()

    ax.set_ylim(bottom=0)

    plt.xlabel(xaxis_label)
    plt.ylabel("Throughput (Gb/s)")

    fig = plt.gcf()
    fig.tight_layout()

    # plt.title(plot_title)
    fig.savefig("plots/" + save_name)

def main():
    # Plot read stream result (Fig. 5)
    plot_dma_threads_size("rd_stream/", 0, 1)

    # Plot Cell KV GET throughput results
    # Fig. 6a
    plot_cell_dma_threads_size("cell-2r", "cell-2r/", 0, 1, 100)
    # Fig. 6b
    plot_cell_dma_threads_size("cell-2r", "cell-2r/", 256, 0, 100)
    # Fig. 6c
    plot_cell_dma_threads_size("cell-2r", "cell-2r/", 0, 16, 500)

    # Plot validation result (Fig. 8)
    plot_cell_validation("cell-val/")
    
    # Plot P2P throughput result (Fig. 9)
    plot_p2p_tput("cell-1r_p2p/", 100)
    
    # Plot mmio fence result (Fig. 10)
    plot_mmio_fence_tput("mmio-wr-bench/")

    return 0

main()

