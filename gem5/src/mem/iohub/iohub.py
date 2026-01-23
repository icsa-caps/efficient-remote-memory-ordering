from m5.params import *
from m5.proxy import *
from m5.objects import ClockedObject

class IOHub(ClockedObject):
    type = "IOHub"
    cxx_class = "gem5::IOHub"
    cxx_header = "mem/iohub/iohub.hh"
    abstract = False

    dev_side_port = ResponsePort("Port to be connected to IOBus")
    mem_side_port = RequestPort("Port to be connected to MemBus")

    queue_size = Param.Unsigned("Number of LSQ entries")
    num_trackers = Param.Unsigned("Number of outstanding requests for cache lines")

    do_fast_writes = Param.Bool(False, "Allocate cache block on write to reduce write latency")
    allow_multiple_pending_reqs = Param.Bool(False, "Whether to wait for send next read request before receiving response")
    read_inorder = Param.Bool(False, "Whether to execute reads in order")
    resp_inorder = Param.Bool(True, "Whether to send read response in order")

    tag_latency = Param.Cycles("Tag lookup latency")
    data_latency = Param.Cycles("Data access latency")
    response_latency = Param.Cycles("Latency for the return path on a miss")

    system = Param.System(Parent.any, "System we belong to")

