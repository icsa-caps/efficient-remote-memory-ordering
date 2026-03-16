#!/bin/bash

CFG_DIR=$(pwd)

RESULTS_DIR=$CFG_DIR/results

if [ ! -d $RESULTS_DIR ]; then
    mkdir $RESULTS_DIR
fi

cd ../cacti

./cacti -infile $CFG_DIR/rlsq.cfg > $RESULTS_DIR/rlsq.out
./cacti -infile $CFG_DIR/rob.cfg > $RESULTS_DIR/rob.out


cd $CFG_DIR

mv rlsq.cfg.out $RESULTS_DIR/rlsq.cfg.out
mv rob.cfg.out $RESULTS_DIR/rob.cfg.out

