#!/bin/bash

RESULTS_DIR=results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

PLOTS_DIR=plots

if [ ! -d $PLOTS_DIR ]; then
    mkdir $PLOTS_DIR
fi

python3 plot.py

