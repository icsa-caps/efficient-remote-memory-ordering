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


# import the m5 (gem5) library created when gem5 is built
import m5

# import all of the SimObjects
from m5.objects import *

# Add the common scripts to our path
m5.util.addToPath("../")
m5.util.addToPath("../../src/python/gem5/components/")

# import the SimpleOpts module
from common import SimpleOpts
from common.Caches import IOCache
from gem5.components.memory.multi_channel import DualChannelDDR3_1600

# import the caches which we made
# from caches import *

# Default to running 'hello', use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
default_binary = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/dev-access/bin/bad-dev",
)

# Binary to execute
SimpleOpts.add_option("binary", nargs="?", default=default_binary)

# Finalize the arguments and grab the args so we can pass it on to our objects
args = SimpleOpts.parse_args()

# create the system we are going to simulate
system = System()

system.m5ops_base = 0xFFFF0000

# Set the clock frequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses

# Create a simple CPU
system.cpu = X86TimingSimpleCPU()

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

system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# North Bridge
system.iobus = IOXBar()
system.bridge = Bridge(delay="50ns")
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
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# Create a DDR3 memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = AddrRange(system.mem_ctrl.dram.device_size)
system.mem_ctrl.port = system.membus.mem_side_ports

# Set up address range
system.mem_ranges = [system.mem_ctrl.dram.range, AddrRange(0xC0000000, size=0x100000)]

# DMA device
system.copy_engine = BadDevice(devicename="Copy Engine", pio_addr=0xC0000000)
system.copy_engine.pio = system.iobus.mem_side_ports

system.workload = SEWorkload.init_compatible(args.binary)

# Create a process for a simple "Hello World" application
process = Process()
# Set the command
# cmd is a list which begins with the executable (like argv)
process.cmd = [args.binary]
# Set the cpu to use the process as its workload and create thread contexts
system.cpu.workload = process
system.cpu.createThreads()

# set up the root SimObject and start the simulation
root = Root(full_system=False, system=system)
# instantiate all of the objects we've created above
m5.instantiate()
# Map device into process memory
process.map(vaddr=0xC0000000, paddr=0xC0000000, size=4, cacheable=False)

print(f"Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
