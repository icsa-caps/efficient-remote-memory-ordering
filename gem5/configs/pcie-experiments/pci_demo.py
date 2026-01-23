from gem5.prebuilt.demo.x86_demo_board import X86DemoBoard
from gem5.resources.resource import obtain_resource
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.simulator import Simulator

import m5
from m5.objects import *

m5.util.addToPath("../")
from common.Caches import IOCache

board = X86DemoBoard()

command = "m5 exit;" \
        + "echo 'This is running on Timing CPU cores.';" \
        + "sleep 1;" \
        + "m5 exit;"

board.set_kernel_disk_workload(
    kernel=obtain_resource("x86-linux-kernel-5.4.49",),
    disk_image=obtain_resource("x86-ubuntu-18.04-img"),
    readfile_contents=command,
)

# Connect pci dma device
board.copy_engine = CopyEngine(ChanCnt=1)

board.copy_engine.host = GenericPciHost(platform=board.pc, 
                          conf_base=0xC000000000000000, 
                          conf_size="256MiB", 
                          conf_device_bits=12, 
                          pci_pio_base=0xBF00000000000000, 
                          pci_mem_base=0xD000000000000000)

board.copy_engine.pio = board.get_io_bus().mem_side_ports
board.copy_engine.dma = board.get_cache_hierarchy().get_cpu_side_port()

board.copy_engine.pci_bus = 0
board.copy_engine.pci_dev = 1
board.copy_engine.pci_func = 1

# workload = obtain_resource("x86-matrix-multiply")
# board.set_se_binary_workload(workload)

simulator = Simulator(board=board)

simulator.run()
