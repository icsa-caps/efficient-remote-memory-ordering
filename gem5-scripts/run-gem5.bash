#!/bin/bash

SCRIPT_DIR=$(pwd)

rm -rf results

cd ../gem5/experiment-scripts

bash run-art-all.bash

# Grab the results
mv results $SCRIPT_DIR/

cd $SCRIPT_DIR

