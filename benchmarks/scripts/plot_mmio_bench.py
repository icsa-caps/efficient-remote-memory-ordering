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

# Column width in latex template
LATEX_TEMPLATE_COLUMNWIDTH = 18

# Plot aspect ration
PLOT_ASPECT_RATIO = 16/6

# PLOT_WIDTH = LATEX_TEMPLATE_COLUMNWIDTH
# PLOT_HEIGHT = PLOT_WIDTH/PLOT_ASPECT_RATIO
PLOT_WIDTH = 3.38
# PLOT_WIDTH = 6
PLOT_HEIGHT = 2.5

# Script name
SCRIPT_NAME = os.path.basename(__file__)

RESULT_MMIO_TPUT = sys.argv[1]
OUTPUT_FOLDER = sys.argv[2]

df = pd.read_csv(RESULT_MMIO_TPUT)
df_mmio_tput = df[df['sfence'] == 0]
df_mmio_tput_sfence = df[df['sfence'] == 1]
    
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

X_COLUMN='size_b'
X_LBL='Message Size (B)'
Y_COLUMN='gbps'
Y_LBL='Bandwidth (Gb/s)'

def plot_bw():
    fig_file_name_pdf = '{}/mmio_bench.pdf'.format(OUTPUT_FOLDER)
    fig_file_name_png = '{}/mmio_bench.png'.format(OUTPUT_FOLDER)

    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))

    df_mmio_tput.plot(kind = 'line', ax=ax, x = X_COLUMN, y = Y_COLUMN, marker='o', color='#377eb8', label='WC + no fence')
    df_mmio_tput_sfence.plot(kind = 'line', ax=ax, x = X_COLUMN, y = Y_COLUMN, marker='x', color='#e41a1c', label='WC + sfence')
    
    plt.axhline(y=100, color='k', linestyle='--', linewidth=0.75)

    # Add labels for the horizontal line
    plt.text(256, 85, 'NIC Bandwidth Limit', color='k', verticalalignment='bottom')

    plt.xlabel(X_LBL)
    plt.ylabel(Y_LBL)
    
    # x_max = df_mmio_tput[X_COLUMN].max()
    # log scale for x-axis
    ax.set_xscale('log', base=2)
    plt.xticks(ticks=[64, 128, 256, 512, 1024, 2048, 4096, 8192], labels=['64', '128', '256', '512', '1K', '2K', '4K', '8K']),
    # ax.xaxis.set_ticks(list(df_mmio_tput[X_COLUMN].unique()))

    # y_max = df_mmio_tput[Y_COLUMN].max()
    # y_lim = y_max * 1.1
    y_lim = 200

    ax.set_ylim(bottom=0, top=y_lim)
    # ax.set_xlim(left=0, right=x_max+1)
    ax.set_xlim(left=64)

    fig = plt.gcf()
    fig.tight_layout()
    fig.savefig(fig_file_name_pdf, dpi=300)
    fig.savefig(fig_file_name_png, dpi=300)

def plot_bw_rw_bar():
    fig_file_name_pdf = '{}/mmio_sizes_tput_bar.pdf'.format(OUTPUT_FOLDER)
    fig_file_name_png = '{}/mmio_sizes_tput_bar.png'.format(OUTPUT_FOLDER)
    
    columns = ['size','operation','num_qp','throughput']

    # Data using multiple clients
    data = [[1024, 'READ', 1, 7.73],
            [1024, 'READ', 2, 15.2],
            [1024, 'WRITE', 1, 13.49],
            [1024, 'WRITE', 2, 27.91]]

    # Data using single client, multiple QPs.
    '''
    data = [[1024, 'READ', 1, 7.77],
            [1024, 'READ', 2, 14.93],
            [1024, 'WRITE', 1, 14.14],
            [1024, 'WRITE', 2, 14.89]]
    '''


    df = pd.DataFrame(data, columns=columns)

    # Prepare the data for plotting
    read_data = df[df['operation'] == 'READ']
    write_data = df[df['operation'] == 'WRITE']

    # Set up figure and axis
    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))

    # Set width of bars
    bar_width = 0.35

    # Set positions of bars on x-axis
    r1 = [0, 1]  # Positions for read operations
    r2 = [x + bar_width for x in r1]  # Positions for write operations

    # Create bars
    ax.bar(r1, read_data['throughput'], width=bar_width, label='READ', color='#e41a1c')
    ax.bar(r2, write_data['throughput'], width=bar_width, label='WRITE', color='#377eb8')

    # Add labels and title
    ax.set_xlabel(X_LBL)
    ax.set_ylabel(Y_LBL)

    # Set x-ticks
    ax.set_xticks([r + bar_width/2 for r in r1])
    ax.set_xticklabels(['1', '2'])

    # Add legend
    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.15), ncol=2)

    # Add grid lines for better readability
    ax.grid(True, linestyle='--', alpha=0.7)

    # Add data values on top of each bar
    for i, v in enumerate(read_data['throughput']):
        ax.text(r1[i], v + 0.5, str(v), ha='center')
        
    for i, v in enumerate(write_data['throughput']):
        ax.text(r2[i], v + 0.5, str(v), ha='center')
    
    y_lim = df['throughput'].max()
    y_lim = y_lim * 1.1
    ax.set_ylim(bottom=0, top=y_lim)

    plt.tight_layout()
    
    fig.savefig(fig_file_name_pdf, dpi=300)
    fig.savefig(fig_file_name_png, dpi=300)

def plot_mpps_rw(): 
    fig_file_name_pdf = '{}/{}_mpps.pdf'.format(EXP_DIR, GRAPH_FILE_NAME)
    fig_file_name_png = '{}/{}_mpps.png'.format(EXP_DIR, GRAPH_FILE_NAME)

    fig, ax = plt.subplots(figsize=(PLOT_WIDTH, PLOT_HEIGHT))

    df_read.plot(kind = 'line', ax=ax, x = X_COLUMN, y = Y_COLUMN_MPPS, marker='o', color='#e41a1c')
    # df_read.plot(kind = 'line', ax=ax, x = X_COLUMN, y = Y_COLUMN_MPPS, marker='o', color='#e41a1c', label='READ')
    # df_write.plot(kind = 'line', ax=ax, x = X_COLUMN, y = Y_COLUMN_MPPS, marker='o', color='#377eb8', label='WRITE')

    plt.xlabel(X_LBL)
    plt.ylabel(Y_LBL_MPPS)
    
    x_max = df_read[X_COLUMN].max()
    ax.xaxis.set_ticks([i for i in range(1, x_max+1)])

    y_max = df_write[Y_COLUMN_MPPS].max()
    y_lim = y_max * 1.1
    # y_lim = 80

    ax.set_ylim(bottom=0, top=y_lim)
    ax.set_xlim(left=0, right=x_max+1)
    
    # title
    ax.set_title('RDMA Throughput vs Number of clients')
    
    # Don't show legend
    ax.legend().set_visible(False)

    fig = plt.gcf()
    fig.tight_layout()
    fig.savefig(fig_file_name_pdf, dpi=300)
    fig.savefig(fig_file_name_png, dpi=300)

def main():
    plot_bw()
    # plot_mpps()
    # plot_bw_bar()

if __name__ == '__main__':
    main()
