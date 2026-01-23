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

# Default to running 'hello', use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
default_binary = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/dev-access/bin/inf-loop",
)

parser = argparse.ArgumentParser()

Options.addNoISAOptions(parser)

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

# Create a simple CPU
system.cpus = [X86TimingSimpleCPU() for i in range(args.num_cpus)]

# Create an L1 instruction and data cache
for i in range(args.num_cpus):
    system.cpus[i].icache = L1ICache(args)
    system.cpus[i].dcache = L1DCache(args)

# Connect the instruction and data caches to cpus
for i in range(args.num_cpus):
    system.cpus[i].icache.connectCPU(system.cpus[i])
    system.cpus[i].dcache.connectCPU(system.cpus[i])

# Configure address space
# Constants similar to x86_traits.hh
IO_address_space_base = 0x8000000000000000
pci_config_address_space_base = 0xC000000000000000
interrupts_address_space_base = 0xA000000000000000
APIC_range_size = 1 << 12

# Create a memory bus
system.membus = SystemXBar()
system.membus.badaddr_responder = BadAddr()
system.membus.badaddr_responder.pio = system.membus.default

# Hook the CPU ports up to the membus
for i in range(args.num_cpus):
    system.cpus[i].icache.connectBus(system.membus)
    system.cpus[i].dcache.connectBus(system.membus)

# North Bridge
system.iobus = IOXBar()
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

# create the interrupt controller for the CPU
for i in range(args.num_cpus):
    system.cpus[i].createInterruptController()
    system.cpus[i].interrupts[0].pio = system.membus.mem_side_ports
    system.cpus[i].interrupts[0].int_requestor = system.membus.cpu_side_ports
    system.cpus[i].interrupts[0].int_responder = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# Create a DDR4 memory controller
system.mem_ctrl0 = MemCtrl()
# system.mem_ctrl0.dram = DDR4_2400_8x8()
system.mem_ctrl0.dram = DDR3_1600_8x8()
system.mem_ctrl0.dram.range = AddrRange(0, size=system.mem_ctrl0.dram.device_size)
system.mem_ctrl0.port = system.membus.mem_side_ports

system.mem_ctrl1 = MemCtrl()
# system.mem_ctrl1.dram = DDR4_2400_8x8()
system.mem_ctrl1.dram = DDR3_1600_8x8()
system.mem_ctrl1.dram.range = AddrRange(system.mem_ctrl0.dram.device_size, size=system.mem_ctrl1.dram.device_size)
system.mem_ctrl1.port = system.membus.mem_side_ports

# Set up address range
system.mem_ranges = [system.mem_ctrl0.dram.range, system.mem_ctrl1.dram.range, AddrRange(0xC0000000, size=0x100000)]

trace_path = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/dev-access/trc/",
    "read-test.txt",
)

system.iocache = IOCache(tag_latency=600, response_latency=600, write_buffers=8)
system.iocache.mem_side = system.membus.cpu_side_ports

# DMA device
system.dma_device = TestRdmaDevice(pio_addr=0xC0000000, pio_latency="10ns", dma_action_latency="10ns",
                                   rdma_trace_file=trace_path, block_requests=False)
system.dma_device.pio = system.iobus.mem_side_ports
system.dma_device.dma = system.iocache.cpu_side

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
    "tests/test-progs/dev-access/bin/idle-loop",
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
m5.setMaxTick(280000000)
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
