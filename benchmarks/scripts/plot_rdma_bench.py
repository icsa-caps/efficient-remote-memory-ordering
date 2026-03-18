#!/usr/bin/env python3

# Script for plotting latency and tput graphs
# Usage: python plot_lat_tput.py <data_file>

import sys
import os
import pathlib
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.font_manager as font_manager
import matplotlib.ticker as ticker
import matplotlib as mpl
from math import log2, log10, ceil
import argparse

# Column width in latex template
LATEX_TEMPLATE_COLUMNWIDTH = 18

# Plot aspect ration
PLOT_ASPECT_RATIO = 16/6

# PLOT_WIDTH = LATEX_TEMPLATE_COLUMNWIDTH
# PLOT_HEIGHT = PLOT_WIDTH/PLOT_ASPECT_RATIO
PLOT_WIDTH = 3.38 # paper
# PLOT_WIDTH = 6
PLOT_HEIGHT = 2 # paper
# PLOT_HEIGHT = 4

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

X_COLUMN_THREAD='threads'
X_LBL_THREADS='Threads'
X_COLUMN_QP='qp'
X_LBL='Number of QPs'
X_COLUMN_BENCH='bench'
Y_COLUMN='tput_gbps'
Y_LBL='Bandwidth (Gb/s)'
Y_COLUMN_MOP='tput_ops'
Y_LBL_MOP='M op/s'
LABELS_TH = ["1 QP", "2 QP", "4 QP"]
LABELS_QP = {"READ": "READ", "WRITE": "WRITE"}
COLORS = ["#e41a1c", "#ff7f00", "#377eb8"]

def plot_threads_vs_tput(df, machine, bench, size, out_dir):
    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))

    num_qps = df[X_COLUMN_QP].unique()
    threads = df[X_COLUMN_THREAD].unique()
    
    file_paths = []
    file_paths.append(os.path.join(out_dir, "threads_vs_tput_m{}_{}_s{}_{}qp.pdf".format(machine, bench, size, num_qps[-1])))
    file_paths.append(os.path.join(out_dir, "threads_vs_tput_m{}_{}_s{}_{}qp.png".format(machine, bench, size, num_qps[-1])))

    y_max = 0
    for i in range(len(num_qps)):
        df_filtered = df[(df[X_COLUMN_BENCH] == bench) & (df[X_COLUMN_QP] == num_qps[i])]
        df_filtered.plot(kind='line', ax=ax, x=X_COLUMN_THREAD, y=Y_COLUMN, marker='o', color=COLORS[i], label= LABELS_TH[i])
        y_max = max(y_max, df_filtered[Y_COLUMN].max())

    plt.xlabel(X_LBL_THREADS)
    plt.ylabel(Y_LBL)
    plt.title('Threads vs Throughput {} benchmark, machine {}'.format(bench, machine))


    # ax.set_xscale('log', base=2)
    plt.xticks(ticks=threads, labels=threads)
    plt.xticks(rotation=45)

    y_lim = y_max * 1.2

    ax.set_ylim(bottom=0, top=y_lim)
    ax.set_xlim(left=0)

    fig = plt.gcf()
    fig.tight_layout()
    for file in file_paths:
        fig.savefig(file, dpi=300)
        print("Saved plot to {}".format(file))

def plot_qps_vs_tput(df, machine, size, threads, out_dir):
    file_paths = []
    file_paths.append(os.path.join(out_dir, "qps_th_vs_tput_m{}_s{}.pdf".format(machine, size)))
    file_paths.append(os.path.join(out_dir, "qps_th_vs_tput_m{}_s{}.png".format(machine, size)))
    
    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))
    
    bench_names = df[X_COLUMN_BENCH].unique()
    num_qps = df[X_COLUMN_QP].unique()

    # For each num QP find max throughput across threads
    df_max = df.groupby([X_COLUMN_BENCH, X_COLUMN_QP])[Y_COLUMN].max().reset_index()
    
    y_max = 0
    i = 0
    for bench in bench_names:
        df_filtered = df_max[(df_max[X_COLUMN_BENCH] == bench)]
        df_filtered.plot(kind='line', ax=ax, x=X_COLUMN_QP, y=Y_COLUMN, marker='o', color=COLORS[i], label= LABELS_QP[bench])
        y_max = max(y_max, df_filtered[Y_COLUMN].max())
        i+=1
    
    plt.xlabel(X_LBL)
    plt.ylabel(Y_LBL)
    plt.title('QPs vs Throughput on {} threads, machine {}'.format(threads, machine))
    
    ax.set_xscale('log', base=2)
    plt.xticks(ticks=num_qps, labels=num_qps)

    y_lim = y_max * 1.2

    ax.set_ylim(bottom=0, top=y_lim)
    ax.set_xlim(left=1)

    fig = plt.gcf()
    fig.tight_layout()
    for file in file_paths:
        fig.savefig(file, dpi=300)
        print("Saved plot to {}".format(file))

def plot_bar_bench(df, machine, size, out_dir):
    file_paths = []
    file_paths.append(os.path.join(out_dir, "bar_m{}_s{}_2qp_final2.pdf".format(machine, size)))
    file_paths.append(os.path.join(out_dir, "bar_m{}_s{}_2qp_final2.png".format(machine, size)))
    
    columns = ['size','operation','num_qp','throughput', 'mpps']
    num_qps = df[X_COLUMN_QP].unique()
    
    reads_tput=[]
    reads_mpps=[]
    for i in num_qps:
        reads_tput.append(df[(df[X_COLUMN_BENCH] == 'READ') & (df[X_COLUMN_QP] == i)][Y_COLUMN].max())
        reads_mpps.append(df[(df[X_COLUMN_BENCH] == 'READ') & (df[X_COLUMN_QP] == i)][Y_COLUMN_MOP].max())
    reads_mpps = [x/1e6 for x in reads_mpps]
    
    writes_tput=[]
    writes_mpps=[]
    for i in num_qps:
        writes_tput.append(df[(df[X_COLUMN_BENCH] == 'WRITE') & (df[X_COLUMN_QP] == i)][Y_COLUMN].max())
        writes_mpps.append(df[(df[X_COLUMN_BENCH] == 'WRITE') & (df[X_COLUMN_QP] == i)][Y_COLUMN_MOP].max())
    writes_mpps = [x/1e6 for x in writes_mpps]

    # fences=[]
    # for i in num_qps:
    #     fences.append(df[(df[X_COLUMN_BENCH] == 'FENCE') & (df[X_COLUMN_QP] == i)][Y_COLUMN].max())

    # data = [
    #         [64, 'READ', 1, reads[0]],
    #         [64, 'READ', 2, reads[1]],
    #         [64, 'READ', 4, reads[2]],
    #         [64, 'FENCE', 1, fences[0]],
    #         [64, 'FENCE', 2, fences[1]],
    #         [64, 'FENCE', 4, fences[2]],
    #         [64, 'WRITE', 1, writes[0]],
    #         [64, 'WRITE', 2, writes[1]],
    #         [64, 'WRITE', 4, writes[2]]
    #     ]
    #
    
    data = [
            [64, 'READ', 1, reads_tput[0], reads_mpps[0]],
            [64, 'READ', 2, reads_tput[1], reads_mpps[1]],
            [64, 'WRITE', 1, writes_tput[0], writes_mpps[0]],
            [64, 'WRITE', 2, writes_tput[1], writes_mpps[1]]
        ]
    df = pd.DataFrame(data, columns=columns)

    # Prepare the data for plotting
    read_data = df[df['operation'] == 'READ']
    # fence_data = df[df['operation'] == 'FENCE']
    write_data = df[df['operation'] == 'WRITE']

    # Set up figure and axis
    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))

    bar_width = 0.35
    group_gap = 0.4  # increase for a larger gap between QP groups
    group_width = 2 * bar_width

    base = [i * (group_width + group_gap) for i in range(len(read_data))]
    r1 = base
    r2 = [p + bar_width for p in base]
    # r3 = [p + 2 * bar_width for p in base]

    # Set width of bars
    # bar_width = 0.35
    #
    # # Set positions of bars on x-axis
    # r1 = [0, 1, 2]  # Positions for read operations
    # r2 = [x + bar_width for x in r1]  # Positions for write operations
    # r3 = [x + bar_width for x in r2]  # Positions for fence operations

    # Create bars
    ax.bar(r1, read_data['throughput'], width=bar_width, label='READ', color='#e41a1c')
    # ax.bar(r2, fence_data['throughput'], width=bar_width, label='FENCE', color='#4daf4a')
    ax.bar(r2, write_data['throughput'], width=bar_width, label='WRITE', color='#377eb8')

    # Add labels and title
    ax.set_xlabel(X_LBL)
    ax.set_ylabel(Y_LBL)


    # Set x-ticks
    ax.set_xticks([p - (bar_width / 2) + group_width / 2 for p in base])
    ax.set_xticklabels(['1', '2'])
    # ax.set_xticks([r + bar_width/2 for r in r1])
    # ax.set_xticklabels(['1', '2', '4'])

    # Add legend
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=3)

    # Add grid lines for better readability
    # ax.grid(True, linestyle='--', alpha=0.7)

    # Add data values on top of each bar
    # for i, v in enumerate(read_data['throughput']):
    #     ax.text(r1[i], v + 0.1, str(v), ha='center')

    # for i, v in enumerate(fence_data['throughput']):
    #     ax.text(r2[i], v + 0.1, str(v), ha='center')
        
    # for i, v in enumerate(write_data['throughput']):
    #     ax.text(r2[i], v + 0.1, str(v), ha='center')
    
    y_lim = df['throughput'].max()
    # y_lim = y_lim * 1.1
    y_lim = 15
    ax.set_ylim(bottom=0, top=y_lim)

    # --- Secondary Y-axis for MPPS ---
    max_tput = df['throughput'].max()
    max_mpps = df['mpps'].max()

    if max_tput > 0 and max_mpps > 0:
        scale = max_mpps / max_tput

        ax2 = ax.twinx()
        ax2.set_ylim(
            ax.get_ylim()[0] * scale,
            ax.get_ylim()[1] * scale
        )
        ax2.set_ylabel(Y_LBL_MOP)  # e.g., "MPPS"
    
    # Right y-axis for MPPS (no extra plot)
    # max_mpps = df['mpps'].max()
    # if max_mpps > 0:
    #     scale = max_mpps / y_lim
    #     ax2 = ax.twinx()
    #     ax2.set_ylim(0, y_lim * scale)
    #     ax2.set_ylabel(Y_LBL_MOP)

    plt.tight_layout()
    
    for file in file_paths:
        fig.savefig(file, dpi=300)
        print("Saved plot to {}".format(file))

def main():
    parser = argparse.ArgumentParser(description='Plot latency and throughput graphs.')
    parser.add_argument('data_file', type=str, help='Path to the data file containing latency and throughput data')
    parser.add_argument('--output_dir', type=str, default='.', help='Directory to save the output plots (default: current directory)')
    parser.add_argument('--machine', type=str, default='sm110p', help='CloudLab Machine name (default: sm110p)')
    parser.add_argument('--type', type=str, default='bar', help='Type of plot to generate: ["bar", "qps", "threads"] (default: bar)')

    args = parser.parse_args()
    
    data_file = args.data_file
    output_dir = args.output_dir
    machine = args.machine
    plot_type = args.type

    df = pd.read_csv(data_file)
    bench_names = df[X_COLUMN_BENCH].unique()

    if plot_type == "threads":
        for bench in bench_names:
            print("plotting threads vs throughput for machine: {}, benchmark: {}".format(machine, bench))
            plot_threads_vs_tput(df, machine, bench, 64, output_dir)
    elif plot_type == "bar":
        print("plotting bar benchmark for machine: {}".format(machine))
        plot_bar_bench(df, machine, 64, output_dir)
    elif plot_type == "qps":
        print("plotting qps vs throughput for machine: {}".format(machine))
        plot_qps_vs_tput(df, machine, 64, 16, output_dir)
    else:
        print("Unknown plot type: {}. Supported types are: ['bar', 'qps', 'threads']".format(plot_type))

if __name__ == '__main__':
    main()
