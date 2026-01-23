# Copyright (c) 2015 Jason Power
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import argparse

# import the m5 (gem5) library created when gem5 is built
import m5

# import all of the SimObjects
from m5.objects import *

# Add the common scripts to our path
m5.util.addToPath("../")
m5.util.addToPath("../../src/python/gem5/components/")

# import the SimpleOpts module
from common import Options
from common.Caches import IOCache

# import the caches which we made
from caches import *

from m5.util.convert import toMemorySize, toInteger

import math

def create_two_level_sockets(system, num_cpus, num_sockets, l1i_size, l1d_size, l2d_size):
    system.l2_caches = [L2Cache(l2d_size) for i in range(num_sockets)]
    system.l2_buses = [L2XBar() for i in range(num_sockets)]
    system.cpus = [X86TimingSimpleCPU() for i in range(num_cpus)]

    for i in range(num_sockets):
        system.l2_caches[i].connectCPUSideBus(system.l2_buses[i])

    for i in range(num_cpus):
        # Set up private caches
        system.cpus[i].icache = L1ICache(l1i_size)
        system.cpus[i].dcache = L1DCache(l1d_size)
        system.cpus[i].icache.connectCPU(system.cpus[i])
        system.cpus[i].dcache.connectCPU(system.cpus[i])
        system.cpus[i].icache.connectBus(system.l2_buses[i % num_sockets])
        system.cpus[i].dcache.connectBus(system.l2_buses[i % num_sockets])

# Default to running 'hello', use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))

parser = argparse.ArgumentParser()

Options.addNoISAOptions(parser)
parser.add_argument("--num-sockets", type=int, default=1)

# Cell device args
parser.add_argument("--cell-create-writer", action='store_true', default=False)
parser.add_argument("--cell-write-interval", type=str, default="1us")
parser.add_argument("--cell-num-readers", type=int, default=1)
parser.add_argument("--cell-read-interval", type=str, default="1us")
parser.add_argument("--cell-read-batch", type=int, default=1)
parser.add_argument("--cell-exe", type=str)

parser.add_argument("--use-cache-line-req", action='store_true', default=False)

parser.add_argument("--use-single-read", action='store_true', default=False)

# Finalize the arguments and grab the args so we can pass it on to our objects
args = parser.parse_args()

# create the system we are going to simulate
system = System()

system.m5ops_base = 0xFFFF0000

# Set the clock frequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "3GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses

# Create sockets
create_two_level_sockets(system, args.num_cpus, args.num_sockets, 
                         l1i_size="16KiB", l1d_size="64KiB", l2d_size="256KiB")

# Configure address space
# Constants similar to x86_traits.hh
IO_address_space_base = 0x8000000000000000
pci_config_address_space_base = 0xC000000000000000
interrupts_address_space_base = 0xA000000000000000
APIC_range_size = 1 << 12

# Create a memory bus
# Increase max_routing_table_size to allow more mshrs
system.membus = SystemXBar(max_routing_table_size=2048)
system.membus.badaddr_responder = BadAddr()
system.membus.badaddr_responder.pio = system.membus.default

# Hook the CPU sockets up to the membus
for i in range(args.num_sockets):
    system.l2_caches[i].connectMemSideBus(system.membus)

system.iobus = IOXBar(is_pcie_model=True)
system.bridge = Bridge(delay="250ns")
system.bridge.mem_side_port = system.iobus.cpu_side_ports
system.bridge.cpu_side_port = system.membus.mem_side_ports

# Allow the bridge to pass through:
#  1) kernel configured PCI device memory map address: address range
#     [0xC0000000, 0xFFFF0000). (The upper 64KiB are reserved for m5ops.)
#  2) the bridge to pass through the IO APIC (two pages, already contained in 1),
#  3) everything in the IO address range up to the local APIC, and
#  4) then the entire PCI address space and beyond.
system.bridge.ranges = [
    AddrRange(0xC0000000, 0xFFFF0000),
    AddrRange(IO_address_space_base, interrupts_address_space_base - 1),
    AddrRange(pci_config_address_space_base, Addr.max),
]

# create the interrupt controller for the CPUs
for i in range(args.num_cpus):
    system.cpus[i].createInterruptController()
    system.cpus[i].interrupts[0].pio = system.membus.mem_side_ports
    system.cpus[i].interrupts[0].int_requestor = system.membus.cpu_side_ports
    system.cpus[i].interrupts[0].int_responder = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# Fix size of address space to 2GB
program_addr_space = AddrRange(0x0, size=(1 << 31))

# Create memory controller(s)
dram_interface = DDR3_1600_8x8
NUM_MEMS = 8
system.mem_ctrls = [MemCtrl() for i in range(NUM_MEMS)]
intlv_bits = int(math.log(NUM_MEMS, 2))
intlv_size = max(128, system.cache_line_size.value)
intlv_low_bit = int(math.log(intlv_size, 2))
for i in range(NUM_MEMS):
    system.mem_ctrls[i].dram = dram_interface()
    system.mem_ctrls[i].dram.range = AddrRange(program_addr_space.start, size=program_addr_space.size(), 
                                               intlvHighBit=intlv_low_bit + intlv_bits - 1, xorHighBit=0, 
                                               intlvBits=intlv_bits, intlvMatch=i)
    system.mem_ctrls[i].port = system.membus.mem_side_ports

# Set up address range
system.mem_ranges = [program_addr_space, AddrRange(0xC0000000, size=0x100000)]

# Model a PCIe root complex
system.root_complex = IOHub(queue_size=256, num_trackers=256,
                            allow_multiple_pending_reqs=True, read_inorder=False, resp_inorder=True,
                            tag_latency=50, data_latency=50, response_latency=50)
system.root_complex.mem_side_port = system.membus.cpu_side_ports

# Model PCIe link for DMA
system.dma_link = IOXBar(forward_latency=600, response_latency=600, is_pcie_model=True)
system.dma_link.mem_side_ports = system.root_complex.dev_side_port

# DMA device
# Note that at 3 GHz, 1 cycle takes approx 333ps.
system.dma_device = CellDevice(pio_addr=0xC0000000, pio_latency="10ns", dma_action_latency="3ns",
                                   create_writer=args.cell_create_writer, write_interval=args.cell_write_interval, 
                                   num_readers=args.cell_num_readers, issue_interval=args.cell_read_interval, 
                                   batch_size=args.cell_read_batch, block_requests=True, use_cache_line_req=args.use_cache_line_req, 
                                   max_cell_reads=10000, use_single_req_get=args.use_single_read,
                                   run_p2p=False, p2p_dev_addr=0xC0200000)
system.dma_device.pio = system.iobus.mem_side_ports
system.dma_device.dma = system.dma_link.cpu_side_ports

default_binary = os.path.join(
    thispath,
    "../../",
    args.cell_exe,
    # "tests/test-progs/device-dma/bin/cell",
)

system.workload = SEWorkload.init_compatible(default_binary)

processes = [Process(pid=100+i) for i in range(args.num_cpus)]

# Set the command
# cmd is a list which begins with the executable (like argv)
processes[0].cmd = [default_binary]

# Set the cpu to use the process as its workload and create thread contexts
system.cpus[0].workload = processes[0]
system.cpus[0].createThreads()

idle_prog_path = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/device-dma/bin/idle-loop",
    # "tests/test-progs/hello/bin/x86/linux/hello",
)

for i in range(1, args.num_cpus):
    processes[i].cmd = [idle_prog_path]
    system.cpus[i].workload = processes[i]
    system.cpus[i].createThreads()

# set up the root SimObject and start the simulation
root = Root(full_system=False, system=system)
# instantiate all of the objects we've created above
m5.instantiate()

# Map device into process memory
processes[0].map(vaddr=0xC0000000, paddr=0xC0000000, size=0x100000, cacheable=False)

print(f"Beginning simulation!")
m5.setMaxTick(55000000000)
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
