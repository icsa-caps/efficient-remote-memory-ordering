#!/bin/bash

set -euo pipefail

# script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Install general tools

function system_setup() {
  vendor=$(grep -m 1 "vendor_id" /proc/cpuinfo 2>/dev/null || true)

  sudo apt update
  sudo apt install -y linux-tools-common linux-tools-`uname -r` htop
  sudo apt install -y meson ninja-build pkg-config python3-pip libnuma-dev msr-tools
  pip install pyelftools

  # Install plot packages
  sudo apt install -y python3-pip
  pip install matplotlib pandas

  disable_dvfs

  # Turn off SMT
  if [ $(cat /sys/devices/system/cpu/smt/control) = "on" ]; then
    echo "Disabling SMT (Simultaneous Multithreading)..."
    echo off | sudo tee /sys/devices/system/cpu/smt/control
  fi
}

function install_ofed() {
# Install mellanox OFED
  bash ${SCRIPT_DIR}/install_mlx_ofed.sh
}

function disable_dvfs() {
  if [[ "$vendor" == *"GenuineIntel"* ]]; then
    # Set scaling governor to performance
    if ! sudo cpupower frequency-set --governor performance; then
      echo "cpupower frequency-set failed, falling back to MSR-based DVFS disable..."
      echo -e "\e[31mThis settings doesn't survive reboot!\e[0m"
      bash ${SCRIPT_DIR}/disable_dvfs.sh
    else
      # Disable turbo
      echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
    fi
  fi
}

# If $1 is empty, print usage
if [ $# -eq 0 ]; then
  echo "Usage: $0 {setup | ofed | dvfs |  all}"
  exit 1
fi

# Command line arguments
if [ "$1" == "ofed" ]; then
	install_ofed
elif [ "$1" == "setup" ]; then
  system_setup
elif [ "$1" == "dvfs" ]; then
  disable_dvfs
elif [ "$1" == "all" ]; then
  install_ofed
  system_setup
else
  echo "Usage: $0 {setup | ofed | dvfs | all}"
  exit 1
fi
