# Efficient Remote Memory Ordering

Code for submission to ASPLOS 2026.

### gem5 Simulation

The files for gem5 simulation can be found in the gem5 directory. 
The files are obtained from version 24.1 of gem5.

Requirements for gem5:

* gcc version >= 10
* Clang 7 to 16
* SCons 3.0 or greater
* Python 3.6+
* protobuf 2.1+

Python requirements for plotting results:

* NumPy
* Matplotlib

Scripts for running the gem5 simulations can be found in the directory `gem5-scripts`.

Instructions for reproducing the figures from gem5 simulation:

1. Enter the gem5 script directory: `cd gem5-scripts`
2. Run the script to set up and build gem5: `./setup-gem5.bash`
3. Run the script to run all simulations: `./run-gem5.bash`
4. Set the python command in line 3 of `plot-gem5.bash` (default is `python3`)
5. Run the script to plot all simulation results: `./plot-gem5.bash`

Plots can be found in the directory `gem5-scripts/plots`.

### CACTI

Scripts for building and running CACTI can be found in the directory `cacti-scripts`. 

Instructions for obtaining the CACTI results:

1. `git submodule init`
2. `git submodule update`
3. Enter the CACTI script directory: `cd gem5-scripts`
4. Build CACTI: `./build_cacti.bash`
5. Run CACTI: `./run_cacti.bash`

Results can be found in the directory `cacti-scripts/results`.

