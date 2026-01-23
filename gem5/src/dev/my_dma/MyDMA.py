from m5.objects.Device import DmaDevice
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class MyDMADevice(DmaDevice):
    type = "MyDMADevice"
    cxx_class = "gem5::MyDMADevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    abstract = False

    pio_addr = Param.Addr("Device Address")
    pio_latency = Param.Latency("100ns", "Programmed IO latency")

    device_memory_size = Param.MemorySize("1MiB", "Size of device memory")
    dma_action_latency = Param.Latency("150ns", "Delay to provide to DMA action call")

class TestRdmaDevice(MyDMADevice):
    type = "TestRdmaDevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    cxx_class = "gem5::TestRdmaDevice"
    abstract = False

    rdma_trace_file = Param.String("Path to file containing RDMA request trace")
    use_trace = Param.Bool(True, "Use trace for generating DMA requests.")
    block_requests = Param.Bool(True, "Whether device should block until it receives a response for an outstanding request")
    use_cache_line_req = Param.Bool(False, "Whether device should only issue cache line sized requests")
    num_queue_pairs = Param.Unsigned(1, "Number of reader queues")

class CellDevice(TestRdmaDevice):
    type = "CellDevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    cxx_class = "gem5::CellDevice"
    abstract = False
    use_trace = False
    rdma_trace_file = NULL

    create_writer = Param.Bool(True, "Whether to create a writer thread")
    write_interval = Param.Latency("1us", "Delay between write requests")
    num_readers = Param.Unsigned(1, "Number of reader queues")
    issue_interval = Param.Latency("1us", "Delay between batches of client requests")
    batch_size = Param.Unsigned(1, "Number of cell reads to issue before incurring a issue interval")
    max_cell_reads = Param.Unsigned(1000, "Maximum number of cell read requests to simulate")
    use_single_req_get = Param.Bool(False, "Whether to use the single request version of cell")

    run_p2p = Param.Bool(False, " Whether to issue P2P read requests from a thread as well")
    p2p_dev_addr = Param.Addr(0xC0000000, "Address of P2P device")
    p2p_dma = RequestPort("P2P DMA port")

class SpscDmaDevice(TestRdmaDevice):
    type = "SpscDmaDevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    cxx_class = "gem5::SpscDmaDevice"
    abstract = False
    use_trace = False
    rdma_trace_file = NULL

class P2pWriteDevice(MyDMADevice):
    type = "P2pWriteDevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    cxx_class = "gem5::P2pWriteDevice"
    abstract = False

    dummy_param = Param.Bool(False, "Work around for some compile errors")

class P2pReadDevice(MyDMADevice):
    type = "P2pReadDevice"
    cxx_header = "dev/my_dma/my_dma.hh"
    cxx_class = "gem5::P2pReadDevice"
    abstract = False

    dma_read_port = ResponsePort("Port to be connected to IOXBar")
    req_queue_size = Param.Unsigned(1, "Number of entries for request queue")

    dummy_param = Param.Bool(False, "Work around for some compile errors")

