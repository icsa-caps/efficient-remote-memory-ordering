from m5.objects.Device import BasicPioDevice
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class MmioDevice(BasicPioDevice):
    type = "MmioDevice"
    cxx_class = "gem5::MmioDevice"
    cxx_header = "dev/mmio_device/mmio_device.hh"
    abstract = False

    device_memory_size = Param.MemorySize("512MiB", "Size of device memory")
    device_write_limit = Param.MemorySize("256KiB", "Size of MMIO write range")
