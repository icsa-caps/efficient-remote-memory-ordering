#!/bin/bash

CUR_DIR=$(pwd)

cd ../gem5/experiment-scripts

bash run-art-all.bash

# Grab the results
mv results $CUR_DIR/

cd $CUR_DIR

