#!/bin/bash

SCRIPT_DIR=$(pwd)

cd ../gem5/experiment-scripts

bash run-art-all.bash

# Grab the results
mv results $SCRIPT_DIR/

cd $SCRIPT_DIR

