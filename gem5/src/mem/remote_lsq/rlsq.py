from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class RemoteLsq(ClockedObject):
    type = "RemoteLsq"
    cxx_class = "gem5::RemoteLsq"
    cxx_header = "mem/remote_lsq/rlsq.hh"
    abstract = False

    bus_side_port = ResponsePort("Port to be connected to IOBus")
    cache_side_port = RequestPort("Port to be connected to IOCache")

    queue_size = Param.Unsigned("Number of LSQ entries")
    allow_multiple_pending_reqs = Param.Bool(False, "Whether to wait for send next read request before receiving response")
    sim_correct_order = Param.Bool(True, "Whether to simulate queue that provides correct order")
    push_lat = Param.Cycles(1, "Queue insert latency")
    pop_lat = Param.Cycles(1, "Queue remove latency")


