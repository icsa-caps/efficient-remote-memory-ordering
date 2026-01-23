#include "my_dma_reg_map.hh"

#include "mem/packet.hh"
#include "mem/packet_access.hh"

#include "base/logging.hh"
#include "base/trace.hh"

#include "debug/RDMADevice.hh"
#include "debug/CellDevice.hh"
#include "debug/CellThread.hh"
#include "debug/CellReaderThread.hh"
#include "debug/CellWriterThread.hh"

#include "debug/P2pDevice.hh"

namespace gem5
{
    class DmaCompletionEvent : Event
    {
        private:
        MyDMADevice* device;
        PacketPtr packet;

        public:
        DmaCompletionEvent(MyDMADevice* dev, PacketPtr pkt) :
        Event(Default_Pri, AutoDelete), device(dev), packet(pkt) {}

        void process() override {
            device->maintainRespStatistics(packet);
        }
    };

    MyDMADevice::MyDmaStats::MyDmaStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(numDmaWrReq, statistics::units::Count::get(), "Number of DMA Write Requests"),
      ADD_STAT(numDmaWrResp, statistics::units::Count::get(), "Number of DMA Write Responses"),
      ADD_STAT(totDmaWrBytes, statistics::units::Byte::get(), "Total number of bytes transferred by DMA Writes"),
      ADD_STAT(wrLatency, statistics::units::Tick::get(), "Ticks between write req and write resp"),
      ADD_STAT(numDmaRdReq, statistics::units::Count::get(), "Number of DMA Read Requests"),
      ADD_STAT(numDmaRdResp, statistics::units::Count::get(), "Number of DMA Read Responses"),
      ADD_STAT(totDmaRdBytes, statistics::units::Byte::get(), "Total number of bytes transferred by DMA Reads"),
      ADD_STAT(rdLatency, statistics::units::Tick::get(), "Ticks between read req and read resp"),
      ADD_STAT(delayEndStart, statistics::units::Tick::get(), "Ticks between start and end of trace"),
      ADD_STAT(avgDmaThroughput, statistics::units::Rate<statistics::units::Byte, statistics::units::Second>::get(), 
      "Average number of bytes trf by DMA per second", ((totDmaRdBytes + totDmaWrBytes) * 1000 * 1000 * 1000 * 1000) / delayEndStart)
    {
    wrLatency.init(16); // number of buckets
    rdLatency.init(16); // number of buckets
    }

    Addr MyDMADevice::get_num_regs()
    {
        return pioSize >> 2;
    }

    MyDMADevice::MyDMADevice(const Params &p)
    : DmaDevice(p), stats(this)
    {
        pioAddr = p.pio_addr;
        pioSize = my_dma_reg_addr_map::MY_DMA_LAST_ADDRESS + 8;
        pioDelay = p.pio_latency;
        devMemSize = p.device_memory_size;
        dmaDelay = p.dma_action_latency;
        device_registers = new uint64_t [get_num_regs()];
        if (device_registers == NULL) {
            panic("Unable to allocate memory for DMA device registers.\n");
        }
        device_memory = new uint8_t [devMemSize];
        if (device_memory == NULL) {
            panic("Unable to allocate memory for DMA device memory.\n");
        }
        nextDevAddr = 0;
    }

    MyDMADevice::~MyDMADevice()
    {
        if (device_registers != NULL) {
            delete device_registers;
        }
        if (device_memory != NULL) {
            delete device_memory;
        }
    }

    Addr MyDMADevice::get_reg_addr(Addr paddr)
    {
        return paddr - pioAddr;
    }

    // Handle PIO reads and writes
    
    Tick MyDMADevice::read(PacketPtr pkt)
    {
        Addr reg_addr = get_reg_addr(pkt->getAddr());

        pkt->makeTimingResponse();

        switch(reg_addr) {
        case my_dma_reg_addr_map::MY_DMA_MAGIC_NUMBER:
            pkt->setLE(0x0000681000006810);
            break;
        case my_dma_reg_addr_map::MY_DMA_TARGET_ADDRS:
            pkt->setLE(device_registers[1]);
            break;
        case my_dma_reg_addr_map::MY_DMA_TRANSFR_SIZE:
            pkt->setLE(device_registers[2]);
            break;
        case my_dma_reg_addr_map::MY_DMA_TRANSFER_DIR:
            pkt->setLE(device_registers[3]);
            break;
        default:
            panic("Read from invalid address on MyDMADevice");
            break;
        }

        return pioDelay;
    }

    Tick MyDMADevice::write(PacketPtr pkt)
    {
        Addr reg_addr = get_reg_addr(pkt->getAddr());
        uint32_t data = pkt->getLE<uint64_t>();
        Addr target_addr = 0;
        Tick delay;

        switch(reg_addr) {
        case my_dma_reg_addr_map::MY_DMA_TARGET_ADDRS:
            device_registers[1] = data;
            break;
        case my_dma_reg_addr_map::MY_DMA_TRANSFR_SIZE:
            device_registers[2] = data;
            break;
        case my_dma_reg_addr_map::MY_DMA_TRANSFER_DIR:
            device_registers[3] = data;
            target_addr = device_registers[1];
            // Trigger DMA request
            delay = 0;
            if (data == 0) {
                // DMA Write
                dmaWrite(target_addr, device_registers[3], (Event*)(new DmaCompletionEvent(this, pkt)), device_memory, delay);
                ++stats.numDmaWrReq;
                stats.totDmaWrBytes += device_registers[3];
                reqTime = curTick();
            } else {
                // DMA Read
                dmaRead(target_addr, device_registers[3], (Event*)(new DmaCompletionEvent(this, pkt)), device_memory, delay);
                ++stats.numDmaRdReq;
                stats.totDmaRdBytes += device_registers[3];
                reqTime = curTick();
            }
            break;
        default:
            panic("Write to invalid address on MyDMADevice");
            break;
        }

        pkt->makeTimingResponse();

        return pioDelay;
    }

    void MyDMADevice::maintainRespStatistics(PacketPtr packet)
    {
        if (packet->isRead()) {
            // Update read statistics
            ++stats.numDmaRdResp;
            stats.rdLatency.sample(curTick() - reqTime);
        } else {
            // Update write statistics
            ++stats.numDmaWrResp;
            stats.rdLatency.sample(curTick() - reqTime);
        }
    }
    
    AddrRangeList MyDMADevice::getAddrRanges() const
    {
        assert(pioSize != 0);
        AddrRangeList ranges;
        ranges.push_back(RangeSize(pioAddr, pioSize));
        return ranges;
    }

    // ========= TestRdmaDevice =========

    class TraceSendEvent : Event
    {
    private:
        TestRdmaDevice* device;

    public:
        TraceSendEvent(TestRdmaDevice* dev) : Event(Default_Pri, AutoDelete), device(dev) {}
        void process() override {
            device->send_trace_dma_req();;
        }
    };

    class TraceDmaCompletionEvent : Event
    {
        private:
        TestRdmaDevice* device;
        uint64_t reqId;

        public:
        TraceDmaCompletionEvent(TestRdmaDevice* dev, uint64_t rid) :
        Event(Default_Pri, AutoDelete), device(dev), reqId(rid) {}

        void process() override {
            device->traceDmaResponse(reqId);
        }
    };

    rdma_req_t TestRdmaDevice::read_trace_line(std::string line) {
        rdma_req_t ret;
        std::string delim = ",";
        size_t delim_pos = line.find(delim);
        if (delim_pos == line.npos) {
            ret.op = RDMA_INV;
            return ret;
        }
        // Start from pos 2 and skip the 0x
        ret.start_addr = (Addr) (std::stoull(line.substr(2, delim_pos), 0, 16));
        line = line.substr(delim_pos + 1, line.npos);
        delim_pos = line.find(delim);
        ret.req_size = (Addr) (std::stoull(line.substr(0, delim_pos)));
        line = line.substr(delim_pos + 1, line.npos);
        delim_pos = line.find(delim);
        std::string op = line.substr(0, delim_pos);

        if (op.compare("RDMA_RD") == 0) {
            ret.op = RDMA_RD;
        } else if (op.compare("RDMA_WR") == 0) {
            ret.op = RDMA_WR;
        } else {
            ret.op = RDMA_INV;
        }

        line = line.substr(delim_pos + 1, line.npos);
        uint64_t wr_data = std::stoull(line);
        ret.data = new uint8_t[ret.req_size];
        for (Addr i = 0; i < ret.req_size; ++i) {
            ret.data[i] = 0;
        }
        uint8_t* data_ptr = (uint8_t*) (&wr_data);
        memcpy(ret.data, data_ptr, sizeof(wr_data));
        DPRINTF(RDMADevice, "data = %lld\n", *((uint64_t*)(&(ret.data[0]))));

        return ret;
    }

    void TestRdmaDevice::split_dma_req(std::queue<rdma_req_t>*req_queue, rdma_req_t req)
    {
        // Split a rdma_req_t into individual cache line sized requests and push them 
        // into req_queue in increasing addr order
        Addr remaining_size = req.req_size;
        Addr cur_start_addr = req.start_addr;
        uint32_t index = 0;
        while (remaining_size > 0) {
            rdma_req_t new_req;
            new_req.op = req.op;
            new_req.flags = req.flags;
            new_req.start_addr = cur_start_addr;
            new_req.req_size = 64;
            if (req.data != NULL) {
                new_req.data = new uint8_t[64];
                memcpy(new_req.data, &(req.data[index]), 64);
            } else {
                new_req.data = NULL;
            }
            req_queue->push(new_req);
            remaining_size -= 64;
            cur_start_addr += 64;
            index += 64;
        }
        delete req.data;
    }

    TestRdmaDevice::TestRdmaDevice(const Params &p) : MyDMADevice(p), num_qps(p.num_queue_pairs),
    qpBlocking(p.block_requests), clBlocking(p.use_cache_line_req),
    initial_event([this]{send_trace_dma_req();}, name() + ".event")
    {
        // Handle trace
        if (p.use_trace) {
            std::ifstream tin;
            tin.open(p.rdma_trace_file, std::ios_base::in);
            if (!tin.is_open()) {
                panic("Unable to open trace file\n");
            }
            while (tin) {
                std::string line;
                std::getline(tin, line);
                // std::cout << line << std::endl;
                rdma_req_t req = read_trace_line(line);
                if (req.op != RDMA_INV) {
                    // Drop invalid requests
                    if (clBlocking) {
                        split_dma_req(&(rdma_req_trace), req);
                    } else {
                        rdma_req_trace.push(req);
                    }
                }
            }
            tin.close();
        }

        cur_req_id = 0;
        last_req_id = ULLONG_MAX;
        cpu_dma_base_addr = 0;
        trace_start_tick = 0;

        // std::cout << p.rdma_trace_file << std::endl;
    }

    TestRdmaDevice::~TestRdmaDevice() {
        MyDMADevice::~MyDMADevice();
    }

    void TestRdmaDevice::process_test_event() {
        std::cout << "Processing Test Event" << std::endl;
    }

    void TestRdmaDevice::print_dma_req(rdma_req_t req) {
        std::string operation = (req.op == RDMA_RD) ? "RDMA READ" : ((req.op == RDMA_WR) ? "RDMA WRITE" : "Invalid operation");
        // std::cout << "[" << std::to_string(curTick()) << "]" << operation << 
        //     " from " << std::to_string(req.start_addr) << " for " << std::to_string(req.req_size) << std::endl;
        DPRINTF(RDMADevice, "[%ld] %s from %lx with size %ld\n", curTick(), operation, req.start_addr, req.req_size);
    }

    void TestRdmaDevice::print_trace() {
        rdma_req_t req = rdma_req_trace.front();
        print_dma_req(req);
        rdma_req_trace.pop();
        if (!(rdma_req_trace.empty())) {
            schedule(initial_event, curTick() + 100);
        }   
    }

    void TestRdmaDevice::send_trace_dma_req() {
        if ((num_qps > 1) && rdma_req_trace.empty()) {
            DPRINTF(RDMADevice, "%s: Hack. With multiple queue pairs, multiple events could be scheduled to dequeue the same request from the trace\n",
                    __func__);
            return;
        }
        rdma_req_t req = rdma_req_trace.front();

        // Check if dma address is valid
        Addr dma_req_target_addr = cpu_dma_base_addr + req.start_addr;
        Addr dma_req_bound_addr = dma_req_target_addr + req.req_size;
        Addr cpu_dma_bound_addr = cpu_dma_base_addr + device_registers[2];
        if ((req.start_addr > device_registers[2]) || (dma_req_bound_addr > cpu_dma_bound_addr)) {
            DPRINTF(RDMADevice, "mem_size: %lld, upper_addr: %lld, max_addr: %lld\n", 
                device_registers[2], dma_req_bound_addr, cpu_dma_bound_addr);
            panic("DMA request to invalid address");
        }

        TraceDmaCompletionEvent* ev = new TraceDmaCompletionEvent(this, cur_req_id);
        req.start_addr = dma_req_target_addr;
        req.flags = 0;

        // Destination for read reqs
        // Wrap index to 0 since devMemSize is a power of 2
        Addr mem_i = nextDevAddr & (devMemSize - 1);

        send_dma_req(req, (Event*)ev, &(device_memory[mem_i]));

        rdma_req_trace.pop();

        // Schedule next req from trace
        if (!qpBlocking) {
            if (!(rdma_req_trace.empty())) {
                TraceSendEvent* send_event = new TraceSendEvent(this);
                schedule((Event*) send_event, curTick() + dmaDelay);
            } else {
                last_req_id = cur_req_id - 1;
            }
        }
    }

    void TestRdmaDevice::send_dma_req(rdma_req_t req, Event* DmaCompletionEvent, uint8_t* read_dst) {
        print_dma_req(req);

        // Track pending request
        pending_req_t dma_req;
        dma_req.op = req.op;
        dma_req.req_time = curTick();
        dma_req.req_size = req.req_size;
        dma_req.dev_addr = nextDevAddr;
        dma_req.data_ptr = req.data;
        pending_reqs.insert(std::pair<uint64_t, pending_req_t>(cur_req_id, dma_req));
        
        Tick delay = 0;
        if (req.op == RDMA_RD) {
            dmaPort.dmaAction(MemCmd::ReadReq, req.start_addr, req.req_size, DmaCompletionEvent, read_dst, delay, req.flags);
            nextDevAddr = nextDevAddr + req.req_size;
            ++stats.numDmaRdReq;
            stats.totDmaRdBytes += req.req_size;
        } else if (req.op == RDMA_WR) {
            dmaPort.dmaAction(MemCmd::WriteReq, req.start_addr, req.req_size, DmaCompletionEvent, req.data, delay, req.flags);
            ++stats.numDmaWrReq;
            stats.totDmaWrBytes += req.req_size;
        }

        ++cur_req_id;
    }

    void TestRdmaDevice::traceDmaResponse(uint64_t requestId)
    {
        pending_req_t req = pending_reqs[requestId];
        if (req.op == RDMA_RD) {
            // Check functionality
            Addr mem_index = req.dev_addr & (devMemSize - 1);
            uint64_t* read_ptr = (uint64_t*) (&device_memory[mem_index]);
            for (Addr i = 0; i < req.req_size; i += sizeof(uint64_t)) {
                DPRINTF(RDMADevice, "Device address: %lld, Value: %llx\n", req.dev_addr, *read_ptr);
                ++read_ptr;
            }
            // Update read statistics
            ++stats.numDmaRdResp;
            stats.rdLatency.sample(curTick() - req.req_time);
        } else if (req.op == RDMA_WR) {
            // Update write statistics
            ++stats.numDmaWrResp;
            stats.wrLatency.sample(curTick() - req.req_time);
        }

        // Free data pointer
        delete req.data_ptr;

        // Remove pending request
        pending_reqs.erase(requestId);

        if (qpBlocking) {
            // Check whether there is another dma request to send
            // std::cout << "Test blocking" << std::endl;
            if (!(rdma_req_trace.empty())) {
                // std::cout << "Test schedule\n" << std::endl;
                TraceSendEvent* send_event = new TraceSendEvent(this);
                schedule((Event*) send_event, curTick() + dmaDelay);
            } else {
                // Last req in trace
                DPRINTF(RDMADevice, "Trace end\n");
                stats.delayEndStart = curTick() - trace_start_tick;
            }
        } else {
            if (requestId == last_req_id) {
                // Last req in trace
                DPRINTF(RDMADevice, "Trace end, reqId: %lld, last_req_id:%lld\n", requestId, last_req_id);
                stats.delayEndStart = curTick() - trace_start_tick;
            }
        }
    }

    Tick TestRdmaDevice::write(PacketPtr pkt)
    {
        Addr reg_addr = get_reg_addr(pkt->getAddr());
        uint32_t data = pkt->getLE<uint64_t>();

        switch(reg_addr) {
        case my_dma_reg_addr_map::MY_DMA_TARGET_ADDRS:
            device_registers[1] = data;
            break;
        case my_dma_reg_addr_map::MY_DMA_TRANSFR_SIZE:
            device_registers[2] = data;
            trace_start_tick = curTick() + pioDelay;
            DPRINTF(RDMADevice, "Trace start\n");
            nextDevAddr = 0;
            cpu_dma_base_addr = device_registers[1];
            for (uint32_t i = 0; i < num_qps; ++i) {
                TraceSendEvent* send_event = new TraceSendEvent(this);
                schedule((Event*) send_event, trace_start_tick);
            }
            break;
        default:
            panic("Write to address not supported on TestRdmaDevice");
            break;
        }

        pkt->makeTimingResponse();
        return pioDelay;
    }
    
    // ========= CellDmaDevice =========
    class StartCellReadEvent : Event
    {
    private:
        CellReaderThread* thread;

    public:
        StartCellReadEvent(CellReaderThread* t) : Event(Default_Pri, AutoDelete), thread(t) {}
        void process() override {
            thread->start_cell_read();
        }
    };

    class CellVersionReadEvent : Event
    {
    private:
        CellReaderThread* thread;

    public:
        CellVersionReadEvent(CellReaderThread* t) : Event(Default_Pri, AutoDelete), thread(t) {}
        void process() override {
            thread->cell_version_read();
        }
    };

    class CellDmaRdCompletionEvent : Event
    {
    private:
        CellDevice* device;
        uint64_t req_id;
        uint32_t thread_id;
        bool versionCheck;

    public:
        CellDmaRdCompletionEvent(CellDevice* d, uint64_t rid, uint32_t tid, bool vc) : Event(Default_Pri, AutoDelete), 
        device(d), req_id(rid), thread_id(tid), versionCheck(vc) {}

        void process() override {
            device->handle_read_completion(req_id, thread_id, versionCheck);
        }
    };

    class StartCellWriteEvent : Event 
    {
    private:
        CellWriterThread* thread;
        uint8_t write_num;

    public:
        StartCellWriteEvent(CellWriterThread* t, uint8_t wn) : Event(Default_Pri, AutoDelete), thread(t), write_num(wn) {}
        void process() override {
            thread->start_cell_write(write_num);
        }
    };

    class CellDmaWrCompletionEvent : Event
    {
    private:
        CellDevice* device;
        uint64_t req_id;
        uint8_t comp_wr_num;

    public:
        CellDmaWrCompletionEvent(CellDevice* d, uint64_t rid, uint8_t cwn) : Event(Default_Pri, AutoDelete),
        device(d), req_id(rid), comp_wr_num(cwn) {}

        void process() override {
            device->handle_write_completion(req_id, comp_wr_num);
        }
    };

    void CellThread::dump_buffer(uint64_t* buf, uint64_t sz)
    {
        uint64_t num_iters = sz / sizeof(uint64_t);
        
        for (uint64_t i = 0; i < num_iters; ++i) {
            DPRINTF(CellThread, " %016lx ", buf[i]);
        }
        DPRINTF(CellThread, "\n");
    }


    CellReaderThread::CellReaderThread()
    {
        CellThread();
        src = 0;
        tmp = NULL;
    }

    void CellReaderThread::init(CellDevice* devp, uint32_t tid, Addr srcv, uint64_t csize)
    {
        dev = devp;
        src = srcv;
        cell_size = csize;
        tmp = (uint64_t*) (new uint8_t[cell_size]);
        thread_id = tid;
        tmp_index = 0;
    }

    CellReaderThread::~CellReaderThread()
    {
        delete tmp;
    }

    void CellReaderThread::issue_from_queue(bool isFirstCL)
    {
        assert(issue_queue.size() > 0);

        rdma_req_t issue_req = issue_queue.front();
        // Buffer for read data
        uint8_t* dst = new uint8_t[issue_req.req_size];
        dev->send_cell_dma_rd_req(issue_req, thread_id, dst, false, isFirstCL);
        issue_queue.pop();
    }

    void CellReaderThread::start_cell_read() 
    {
        if (dev->get_maxed_reqs()) {
            DPRINTF(CellReaderThread, "%s: Not starting new Cell request\n", __func__);
            return;
        }

        rdma_req_t cell_read_req;
        cell_read_req.data = NULL;
        cell_read_req.flags = 0;
        cell_read_req.op = RDMA_RD;
        cell_read_req.start_addr = src;
        cell_read_req.req_size = cell_size;

        if (dev->checkClBlocking()) {
            DPRINTF(CellReaderThread, "%s: Splitting requests into cache lines\n", __func__);
            dev->split_dma_req(&issue_queue, cell_read_req);
            DPRINTF(CellReaderThread, "%s: Number of requests: %d\n", __func__, issue_queue.size());
        } else {
            issue_queue.push(cell_read_req);
        }

        issue_from_queue(true);
    }

    void CellReaderThread::cell_version_read()
    {
        rdma_req_t cell_read_req;
        cell_read_req.data = NULL;
        cell_read_req.flags = 0;
        cell_read_req.op = RDMA_RD;
        cell_read_req.start_addr = src;
        cell_read_req.req_size = 64;

        // Buffer for read data
        uint8_t* dst = new uint8_t[64];

        dev->send_cell_dma_rd_req(cell_read_req, thread_id, dst, true, false);
    }

    void CellReaderThread::copy_cache_line(uint64_t* dst)
    {
        DPRINTF(CellReaderThread, "%s: Cache line read complete. Current Index: %d\n", __func__, tmp_index);
        uint64_t num_iters = 64 / sizeof(uint64_t);
        for (uint64_t i = 0; i < num_iters; ++i) {
            tmp[tmp_index] = dst[i];
            ++tmp_index;
        }
        delete dst;
    }

    bool CellReaderThread::handle_first_read(uint64_t* dst)
    {
        if (dev->checkClBlocking()) {
            DPRINTF(CellReaderThread, "%s: Clearing tmp_index from value %d\n", __func__, tmp_index);
            copy_cache_line(dst);
            tmp_index = 0;
        } else {
            DPRINTF(CellReaderThread, "%s: Dumping dst buffer...\n", __func__);
            dump_buffer(dst, cell_size);
            // Copy data into temp buffer
            uint64_t num_iters = cell_size / sizeof(uint64_t);
            for (uint64_t i = 0; i < num_iters; ++i) {
                tmp[i] = dst[i];
            }
            delete dst;
        }

        first_header = tmp[0];
        if ((first_header & 1) == 1) {
            // Read started during a write, should retry this read
            DPRINTF(CellReaderThread, "%s: Retry read, first_header = %ld\n", __func__, first_header);
            return true;
        }
        
        DPRINTF(CellReaderThread, "%s: Dumping tmp buffer...\n", __func__);
        dump_buffer(tmp, cell_size);
        return false;
    }

    uint8_t CellReaderThread::handle_second_read(uint64_t* dst)
    {
        second_header = dst[0];
        delete dst;

        if ((first_header & 1) == 1) {
            // Catch first read failure
            DPRINTF(CellReaderThread, "%s: Retry read, first_header = %ld, second_header = %ld\n", __func__, first_header, second_header);
            return 1;
        }
        if (second_header != first_header) {
            DPRINTF(CellReaderThread, "%s: Retry read, first_header = %ld, second_header = %ld\n", __func__, first_header, second_header);
            return 2;
        }
        DPRINTF(CellReaderThread, "%s: Read success. Version number: %ld. Verifying read data...\n", __func__, first_header);
        uint64_t num_iters = cell_size / sizeof(uint64_t);
        for (uint64_t i = 0; i < num_iters; ++i) {
            if (tmp[i] != first_header) {
                DPRINTF(CellReaderThread, "%s: Read data verification failed. Version number: %ld, Read value: %ld\n", __func__, first_header, tmp[i]);
                DPRINTF(CellReaderThread, "%s: Dumping thread read buffer...\n", __func__);
                dump_buffer(tmp, cell_size);
                return 3;
            }
        }
        DPRINTF(CellReaderThread, "%s: Version number: %ld. Read data verification success!\n", __func__, first_header);
        return 0;
    }

    uint8_t CellReaderThread::handle_single_read(uint64_t* dst)
    {
        if (dev->checkClBlocking()) {
            copy_cache_line(dst);
            tmp_index = 0;
        } else {
            DPRINTF(CellReaderThread, "%s: Dumping dst buffer...\n", __func__);
            dump_buffer(dst, cell_size);
            // Copy data into temp buffer
            uint64_t num_iters = cell_size / sizeof(uint64_t);
            for (uint64_t i = 0; i < num_iters; ++i) {
                tmp[i] = dst[i];
            }
            delete dst;
        }

        DPRINTF(CellReaderThread, "%s: Dumping tmp buffer...\n", __func__);
        dump_buffer(tmp, cell_size);

        first_header = tmp[0];
        uint32_t footer_index = (cell_size / sizeof(uint64_t)) - 1;
        second_header = tmp[footer_index];

        if ((first_header & 1) == 1) {
            // Read started during a write, should retry this read
            DPRINTF(CellReaderThread, "%s: Retry read, first_header = %ld\n", __func__, first_header);
            return 1;
        }
        
        if (second_header != first_header) {
            DPRINTF(CellReaderThread, "%s: Retry read, first_header = %ld, second_header = %ld\n", __func__, first_header, second_header);
            return 1;
        }

        DPRINTF(CellReaderThread, "%s: Read success. Version number: %ld. Verifying read data...\n", __func__, first_header);
        uint64_t num_iters = cell_size / sizeof(uint64_t);
        for (uint64_t i = 0; i < num_iters; ++i) {
            if (tmp[i] != first_header) {
                DPRINTF(CellReaderThread, "%s: Read data verification failed. Version number: %ld, Read value: %ld\n", __func__, first_header, tmp[i]);
                return 3;
            }
        }
        DPRINTF(CellReaderThread, "%s: Version number: %ld. Read data verification success!\n", __func__, first_header);

        return 0;
    }

    CellWriterThread::CellWriterThread()
    {
        CellThread();
        dst = 0;
        src = NULL;
    }

    CellWriterThread::~CellWriterThread()
    {
        delete src;
    }

    void CellWriterThread::init(CellDevice* devp, Addr dstv, uint64_t csize)
    {
        dev = devp;
        dst = dstv;
        cell_size = csize;
        version_num = 0;
        src = (uint64_t*) (new uint8_t[cell_size]);
    }

    void CellWriterThread::start_cell_write(uint8_t write_number)
    {
        rdma_req_t cell_write_req;
        cell_write_req.flags = 0;
        cell_write_req.op = RDMA_WR;

        uint32_t footer_index = (cell_size / sizeof(uint64_t)) - 1;

        switch (write_number) {
        case 0:
            // Write header cache line
            // Starting cell write, increment version number
            ++version_num;
            // Update write data with new version number
            src[0] = version_num;
            cell_write_req.data = (uint8_t*) src;
            cell_write_req.start_addr = dst;
            cell_write_req.req_size = 64;
            DPRINTF(CellWriterThread, "%s: Sending starting header write, version number: %lld\n", __func__, version_num);
            break;
        case 1:
            // Write odd version number in footer cache line
            src[footer_index] = version_num;
            cell_write_req.data = (uint8_t*) (&src[footer_index - 7]);
            cell_write_req.start_addr = dst + cell_size - 64;
            cell_write_req.req_size = 64;
            DPRINTF(CellWriterThread, "%s: Sending starting footer write, version number: %lld\n", __func__, version_num);
            break;
        case 2:
            // Write body data
            // Make version number even again
            ++version_num;
            // Update body data
            for (uint64_t i = 1; i < footer_index; ++i) {
                src[i] = version_num;
            }
            cell_write_req.data = (uint8_t*) src;
            cell_write_req.start_addr = dst;
            cell_write_req.req_size = cell_size;
            DPRINTF(CellWriterThread, "%s: Sending data write, version number: %lld\n", __func__, version_num);
            break;
        case 3:
            // Write even version number in footer cache line
            src[footer_index] = version_num;
            cell_write_req.data = (uint8_t*) (&src[footer_index - 7]);
            cell_write_req.start_addr = dst + cell_size - 64;
            cell_write_req.req_size = 64;
            DPRINTF(CellWriterThread, "%s: Sending ending footer write, version number: %lld\n", __func__, version_num);
            break;
        case 4:
            // Release even version number in header
            src[0] = version_num;
            cell_write_req.data = (uint8_t*) src;
            cell_write_req.start_addr = dst;
            cell_write_req.req_size = 64;
            DPRINTF(CellWriterThread, "%s: Sending ending header write, version number: %lld\n", __func__, version_num);
            break;
        default:
            panic("Call to start_cell_write with invalid write_number");
            break;
        }
        
        dev->send_cell_dma_wr_req(cell_write_req, write_number);
    }

    CellDevice::CellStats::CellStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(numCellWrReq, statistics::units::Count::get(), "Number of Cell Write Requests"),
      ADD_STAT(numCellRdReq, statistics::units::Count::get(), "Number of Cell Read Requests"),
      ADD_STAT(totCellRdBytes, statistics::units::Byte::get(), "Total number of bytes read"),
      ADD_STAT(numTornCellRd, statistics::units::Count::get(), "Number of Torn Cell Reads"),
      ADD_STAT(numSuccCellRd, statistics::units::Count::get(), "Number of Successful Cell Reads"),
      ADD_STAT(numFailCellRd, statistics::units::Count::get(), "Number of Cell Reads that fail and must be retried"),
      ADD_STAT(cellRdTornRate, statistics::units::Rate<statistics::units::Count, statistics::units::Count>::get(), 
      "Fraction of Cell reads that are torn", numTornCellRd / numCellRdReq),
      ADD_STAT(cellRdDelayEndStart, statistics::units::Tick::get(), "Ticks between issuing first and receiving last rd"),
      ADD_STAT(avgCellRdThroughput, statistics::units::Rate<statistics::units::Byte, statistics::units::Second>::get(), 
      "Average number of bytes trf by DMA per second", (totCellRdBytes * 1000 * 1000 * 1000 * 1000) / cellRdDelayEndStart),
      ADD_STAT(numP2pCellRdIssued, statistics::units::Count::get(), "Number of Cell read requests issued to p2p device"),
      ADD_STAT(numP2pCellRdCompleted, statistics::units::Count::get(), "Number of Cell read requests from p2p device completed"),
      ADD_STAT(numCpuCellRd, statistics::units::Count::get(), "Number of Cell read requests to CPU"),
      ADD_STAT(avgCellGetMops, statistics::units::Rate<statistics::units::Count, statistics::units::Second>::get(),
      "Millions of GET operations per second", ((numCpuCellRd + numP2pCellRdCompleted) * 1000 * 1000) / cellRdDelayEndStart),
      ADD_STAT(avgCpuCellGetMops, statistics::units::Rate<statistics::units::Count, statistics::units::Second>::get(),
      "Millions of GET operations per second", (numCpuCellRd * 1000 * 1000) / cellRdDelayEndStart),
      ADD_STAT(cpuCellRdBytes, statistics::units::Byte::get(), "Total number of bytes read from CPU"),
      ADD_STAT(p2pCellRdBytes, statistics::units::Byte::get(), "Total number of bytes read from P2P device"),
      ADD_STAT(cpuCellRdThroughput, statistics::units::Rate<statistics::units::Byte, statistics::units::Second>::get(), 
      "Average number of bytes read from CPU per second", (cpuCellRdBytes * 1000 * 1000 * 1000 * 1000) / cellRdDelayEndStart),
      ADD_STAT(p2pCellRdThroughput, statistics::units::Rate<statistics::units::Byte, statistics::units::Second>::get(), 
      "Average number of bytes read from P2P device per second", (p2pCellRdBytes * 1000 * 1000 * 1000 * 1000) / cellRdDelayEndStart),
      ADD_STAT(interIssueTime, statistics::units::Tick::get(), "Ticks between successive cell rds issued to CPU")
    {
        interIssueTime.init(16); // 16 buckets
    }

    CellDevice::CellDevice(const Params &p) : TestRdmaDevice(p), use_writer(p.create_writer), num_readers(p.num_readers), 
    max_cell_reads(p.max_cell_reads), cur_cell_reads(0), last_rd_req_id(ULLONG_MAX), devMaxedReqs(false), 
    issue_latency(p.issue_interval), batch_size(p.batch_size), batch_req_count(0), batch_resp_count(0),
    write_interval(p.write_interval), isSingleReq(p.use_single_req_get), issueP2P(p.run_p2p), p2p_dev_addr(p.p2p_dev_addr), 
    prev_dma_send_time(0), completed_p2p_cell_rds(0), p2p_cell_read_bytes(0), tot_cell_read_bytes(0), stat_last_issue_time(0),
    p2pPort(this, sys, p.sid, p.ssid), cell_stats(this)
    {
        pioSize = cell_dma_reg_addr_map::CELL_DMA_LAST_ADDR + 8;

        if (issueP2P) {
            queues = new CellReaderThread[num_readers + 1];
        } else {
            queues = new CellReaderThread[num_readers];
        }
        
        writer_queue = NULL;
        cell_device_registers = new uint64_t[get_num_regs()];
        // Simplify things by blocking requests if blocking cache lines
        if (clBlocking) {
            assert(qpBlocking);
        }
    }

    CellDevice::~CellDevice() 
    {
        delete [] queues;
        delete writer_queue;
        delete cell_device_registers;
    }

    Port& CellDevice::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "p2p_dma") {
            return p2pPort;
        }
        return DmaDevice::getPort(if_name, idx);
    }

    Tick CellDevice::write(PacketPtr pkt)
    {
        Addr reg_addr = get_reg_addr(pkt->getAddr());
        uint64_t data = pkt->getLE<uint64_t>();

        switch(reg_addr) {
        case cell_dma_reg_addr_map::CELL_DMA_MEM_ADDR:
            cell_device_registers[1] = data;
            break;
        case cell_dma_reg_addr_map::CELL_DMA_CELL_SIZE:
            cell_device_registers[2] = data;
            break;
        case cell_dma_reg_addr_map::CELL_DMA_CTRL_REG:
            trace_start_tick = curTick() + pioDelay;
            for (uint32_t i = 0; i < num_readers; ++i) {
                queues[i].init(this, i, (Addr)(cell_device_registers[1]), cell_device_registers[2]);
                DPRINTF(CellDevice, "Starting Cell reader thread %d at tick %ld\n", i, trace_start_tick);
                schedule((Event*) (new StartCellReadEvent(&(queues[i]))), trace_start_tick);
            }

            if (use_writer) {
                // Create a writer thread
                writer_queue = new CellWriterThread();
                writer_queue->init(this, (Addr)(cell_device_registers[1]), cell_device_registers[2]);
                DPRINTF(CellDevice, "Starting Cell writer thread at tick %ld\n", trace_start_tick);
                schedule((Event*) (new StartCellWriteEvent(writer_queue, 0)), trace_start_tick);
            }

            if (issueP2P) {
                queues[num_readers].init(this, num_readers, p2p_dev_addr, cell_device_registers[2]);
                DPRINTF(CellDevice, "Starting Cell reader thread %d at tick %ld\n", num_readers, trace_start_tick);
                schedule((Event*) (new StartCellReadEvent(&(queues[num_readers]))), trace_start_tick);
            }

            break;
        default:
            panic("Write to address not supported on CellDevice");
            break;
        }

        pkt->makeTimingResponse();
        return pioDelay;
    }

    void CellDevice::send_p2p_dma(rdma_req_t req, Event* DmaCompletionEvent, uint8_t* read_dst) {
        print_dma_req(req);

        // Track pending request
        pending_req_t dma_req;
        dma_req.op = req.op;
        dma_req.req_time = curTick();
        dma_req.req_size = req.req_size;
        dma_req.dev_addr = nextDevAddr;
        dma_req.data_ptr = req.data;
        pending_reqs.insert(std::pair<uint64_t, pending_req_t>(cur_req_id, dma_req));
        
        Tick delay = 0;
        if (req.op == RDMA_RD) {
            p2pPort.dmaAction(MemCmd::ReadReq, req.start_addr, req.req_size, DmaCompletionEvent, read_dst, delay, req.flags);
            nextDevAddr = nextDevAddr + req.req_size;
            ++stats.numDmaRdReq;
            stats.totDmaRdBytes += req.req_size;
        } else if (req.op == RDMA_WR) {
            p2pPort.dmaAction(MemCmd::WriteReq, req.start_addr, req.req_size, DmaCompletionEvent, req.data, delay, req.flags);
            ++stats.numDmaWrReq;
            stats.totDmaWrBytes += req.req_size;
        }

        ++cur_req_id;
    }

    void CellDevice::send_cell_dma_rd_req(rdma_req_t req, uint32_t thread_id,  uint8_t* dest, bool isVersionRead, bool isFirstCL)
    {
        if (!isVersionRead) {
            if (isFirstCL) {

                // Send out max_cell_reads to CPU
                if (thread_id < num_readers) {
                    ++cur_cell_reads;
                    ++cell_stats.numCpuCellRd;
                }

                // Keep track of statistics
                ++cell_stats.numCellRdReq;
                if (issueP2P && (thread_id == num_readers)) {
                    // Request issued to P2P device
                    ++cell_stats.numP2pCellRdIssued;
                    DPRINTF(CellDevice, "Issuing DMA read to P2P device at address %llx\n", req.start_addr);
                }

                DPRINTF(CellDevice, "%s: Sending data read, thread_id: %ld, req_id: %lld\n", __func__, thread_id, cur_req_id);
                if (cur_cell_reads == max_cell_reads) {
                    // No new cell requests will be sent
                    DPRINTF(CellDevice, "Reached max number of Cell reqs (%ld), not sending new requests\n", max_cell_reads);
                    set_maxed_reqs();
                }
            }
            if (!qpBlocking) {
                Tick t1 = curTick() + dmaDelay;
                
                if (!isSingleReq) {
                    // Send out read of version number
                    DPRINTF(CellDevice, "%s: Device does not block, chaining version read\n", __func__);
                    CellVersionReadEvent *cvr_ev = new CellVersionReadEvent(&queues[thread_id]);
                    Tick t0 = prev_dma_send_time + dmaDelay;
                    Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                    schedule((Event *)cvr_ev, sched_time);
                    prev_dma_send_time = sched_time;
                }

                // Send out next read in batch as well
                // Requests to P2P device not part of batch
                if (thread_id < num_readers) {
                    ++batch_req_count;
                }
                if (batch_req_count < batch_size) {
                    StartCellReadEvent *scr_ev = new StartCellReadEvent(&queues[thread_id]);
                    Tick t0 = prev_dma_send_time + dmaDelay;
                    Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                    schedule((Event *)scr_ev, sched_time);
                    prev_dma_send_time = sched_time;
                } else {
                    // Thread is blocked because batch size is reached
                    blocked_threads.push_back(thread_id);
                }
            }
        } else {
            DPRINTF(CellDevice, "%s: Sending version read, req_id: %lld\n", __func__, cur_req_id);
        }

        if (issueP2P) {
            unordered_receive_buffers.insert(std::pair<uint64_t,uint64_t*>(cur_req_id, (uint64_t*)dest));
        } else {
            receive_buffers.push((uint64_t*) dest);
            req_id_queue.push(cur_req_id);
        }

        CellDmaRdCompletionEvent* comp_ev = new CellDmaRdCompletionEvent(this, cur_req_id, thread_id, isVersionRead);
        // Only track delay for CPU cell reads
        if (thread_id < num_readers) {
            last_req_id = cur_req_id;
            last_rd_req_id = last_req_id;
        }

        if (thread_id < num_readers) {
            // Keep track of inter issue time for CPU cell reads
            if (stat_last_issue_time > 0) {
                cell_stats.interIssueTime.sample(curTick() - stat_last_issue_time);
            }
            stat_last_issue_time = curTick();

            send_dma_req(req, (Event*) comp_ev, dest);
        } else {
            // Send from P2P port
            send_p2p_dma(req, (Event*) comp_ev, dest);
        }
    }

    void CellDevice::handle_read_completion(uint64_t request_id, uint32_t thread_id, bool isVersionRead)
    {
        uint64_t* dst = NULL;

        DPRINTF(CellDevice, "%s: Handling dma completion, thread_id: %ld\n", __func__, thread_id);

        if (issueP2P) {
            // With P2P transfers, responses from different devices might not be ordered
            std::unordered_map<uint64_t,uint64_t*>::iterator completed_req = unordered_receive_buffers.find(request_id);
            // Outstanding entry better exist!
            assert(completed_req != unordered_receive_buffers.end());
            dst = completed_req->second;
            unordered_receive_buffers.erase(completed_req);
        } else {
            // Check that responses are received in order
            assert(request_id == req_id_queue.front());
            dst = receive_buffers.front();
            req_id_queue.pop();
            receive_buffers.pop();
        }

        if (isSingleReq) {
            if (clBlocking && (queues[thread_id].get_num_remaining_cl() > 0)) {
                // Copy data out of cache line
                queues[thread_id].copy_cache_line(dst);
                // Not done with the first read request
                queues[thread_id].issue_from_queue(false);
            } else {
                DPRINTF(CellDevice, "%s: Handling request completion, req_id: %lld\n", __func__, request_id);
                uint8_t test_result = queues[thread_id].handle_single_read(dst);
                if (test_result == 1) {
                    ++cell_stats.numFailCellRd;
                } else if (test_result == 3) {
                    ++cell_stats.numTornCellRd;
                } else if (test_result == 0) {
                    ++cell_stats.numSuccCellRd;
                }

                if (thread_id == num_readers) {
                    // Completed P2P request
                    DPRINTF(CellDevice, "%s: Received completion from P2P device\n", __func__);
                    ++completed_p2p_cell_rds;
                }

                // Send out next read
                if (qpBlocking) {
                    if (thread_id < num_readers) {
                        ++batch_req_count;
                    }
                    Tick issue_delay = dmaDelay;
                    if (batch_req_count >= batch_size) {
                        issue_delay += issue_latency;
                        batch_req_count = 0;
                    }
                    Tick t1 = curTick() + issue_delay;
                    Tick t0 = prev_dma_send_time + dmaDelay;
                    Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                    StartCellReadEvent *scr_ev = new StartCellReadEvent(&queues[thread_id]);
                    schedule((Event *)scr_ev, sched_time);
                    prev_dma_send_time = sched_time;
                } else {
                    // Not blocking
                    if (thread_id < num_readers) {
                        ++batch_resp_count;
                    }
                    if (batch_resp_count >= batch_size) {
                        // Schedule next batch of reads
                        batch_req_count = 0;
                        batch_resp_count = 0;
                        // Unblock threads to start new batch
                        while (!blocked_threads.empty()) {
                            uint32_t tid = blocked_threads.back();
                            StartCellReadEvent *scr_ev = new StartCellReadEvent(&queues[tid]);
                            Tick t1 = curTick() + dmaDelay + issue_latency;
                            Tick t0 = prev_dma_send_time + dmaDelay;
                            Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                            schedule((Event *)scr_ev, sched_time);
                            prev_dma_send_time = sched_time;
                            blocked_threads.pop_back();
                        }
                    }
                }
            }
        } else {
            // Using double read
            if (!isVersionRead) {
                if (clBlocking && (queues[thread_id].get_num_remaining_cl() > 0)) {
                    DPRINTF(CellDevice, "%s: Received cache line, req_id: %lld\n", __func__, request_id);
                    // Copy data out of cache line
                    queues[thread_id].copy_cache_line(dst);
                    // Not done with the first read request
                    queues[thread_id].issue_from_queue(false);
                } else {
                    // Got data with initial version number
                    DPRINTF(CellDevice, "%s: Handling data read completion, req_id: %lld\n", __func__, request_id);
                    bool need_retry = queues[thread_id].handle_first_read(dst);
                    if (need_retry) {
                        // Retry read
                        ++cell_stats.numFailCellRd;
                    }
                    if (qpBlocking) {
                        // Send out read of version number
                        DPRINTF(CellDevice, "%s: Device blocks, sending version read, req_id: %lld\n", __func__, request_id);
                        CellVersionReadEvent *cvr_ev = new CellVersionReadEvent(&queues[thread_id]);
                        schedule((Event *)cvr_ev, curTick());
                    }
                }
            } else {
                // isVersionRead
                DPRINTF(CellDevice, "%s: Handling version read completion, req_id: %lld\n", __func__, request_id);
                // Got second version number
                uint8_t test_result = queues[thread_id].handle_second_read(dst);
                // Note that if test_result == 1, fail read stat already recorded when handling first read
                if (test_result == 2) {
                    // Failed cell read
                    ++cell_stats.numFailCellRd;
                } else if (test_result == 0) {
                    // Cell read succeeded
                    ++cell_stats.numSuccCellRd;
                }
                else if (test_result == 3) {
                    // Undetected torn read
                    ++cell_stats.numTornCellRd;
                }

                if (thread_id == num_readers) {
                    // Completed P2P request
                    DPRINTF(CellDevice, "%s: Received completion from P2P device\n", __func__);
                    ++completed_p2p_cell_rds;
                }

                // Send out next read
                if (qpBlocking) {
                    if (thread_id < num_readers) {
                        ++batch_req_count;
                    }
                    Tick issue_delay = dmaDelay;
                    if (batch_req_count >= batch_size) {
                        issue_delay += issue_latency;
                        batch_req_count = 0;
                    }
                    Tick t0 = prev_dma_send_time + dmaDelay;
                    Tick t1 = curTick() + issue_delay;
                    Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                    StartCellReadEvent *scr_ev = new StartCellReadEvent(&queues[thread_id]);
                    schedule((Event *)scr_ev, sched_time);
                    prev_dma_send_time = sched_time;
                } else {
                    // Not blocking
                    if (thread_id < num_readers) {
                        ++batch_resp_count;
                    }
                    if (batch_resp_count >= batch_size) {
                        // Schedule next batch of reads
                        batch_req_count = 0;
                        batch_resp_count = 0;
                        // Unblock threads to start new batch
                        while (!blocked_threads.empty()) {
                            uint32_t tid = blocked_threads.back();
                            StartCellReadEvent *scr_ev = new StartCellReadEvent(&queues[tid]);
                            Tick t0 = prev_dma_send_time + dmaDelay;
                            Tick t1 = curTick() + dmaDelay + issue_latency;
                            Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
                            schedule((Event *)scr_ev, sched_time);
                            prev_dma_send_time = sched_time;
                            blocked_threads.pop_back();
                        }
                    }
                }
            }
        }

        // Clean up and record stats
        ++stats.numDmaRdResp;
        pending_req_t req = pending_reqs[request_id];
        tot_cell_read_bytes += req.req_size;

        if (request_id == last_req_id) {
            // Last req in trace
            stats.delayEndStart = curTick() - trace_start_tick;
        }
        if (request_id == last_rd_req_id) {
            // Last request for timing should never be a request to the P2P device.
            assert(thread_id < num_readers);
            DPRINTF(CellDevice, "Received response for last rd req, reqId: %lld, last_rd_req_id:%lld\n", request_id, last_rd_req_id);
            cell_stats.cellRdDelayEndStart = curTick() - trace_start_tick;
            cell_stats.numP2pCellRdCompleted = completed_p2p_cell_rds;
            cell_stats.p2pCellRdBytes = p2p_cell_read_bytes;
            cell_stats.totCellRdBytes = tot_cell_read_bytes;
        }
        
        if (thread_id < num_readers) {
            cell_stats.cpuCellRdBytes += req.req_size;
        } else {
            p2p_cell_read_bytes += req.req_size;
        }
        stats.rdLatency.sample(curTick() - req.req_time);
        // Remove pending request
        pending_reqs.erase(request_id);
    }

    void CellDevice::send_cell_dma_wr_req(rdma_req_t req, uint8_t cur_write_num)
    {
        if (cur_write_num == 0) {
            ++cell_stats.numCellWrReq;
        }
        DPRINTF(CellDevice, "%s: Sending write %d, req_id: %lld\n", __func__, cur_write_num, cur_req_id);
        CellDmaWrCompletionEvent* comp_ev = new CellDmaWrCompletionEvent(this, cur_req_id, cur_write_num);
        last_req_id = cur_req_id;
        send_dma_req(req, (Event*) comp_ev, NULL);
    }

    void CellDevice::handle_write_completion(uint64_t request_id, uint8_t prev_write_num)
    {
        DPRINTF(CellDevice, "%s: Completed write %d, req_id:%lld\n", __func__, prev_write_num, request_id);
        Tick send_delay = dmaDelay;
        uint8_t next_write_num = prev_write_num + 1;
        if (next_write_num >= 5) {
            next_write_num = 0;
            send_delay += write_interval;
        }

        Tick t0 = prev_dma_send_time + dmaDelay;
        Tick t1 = curTick() + send_delay;
        Tick sched_time = issueP2P ? ((t0 > t1) ? t0 : t1) : t1;
        schedule((Event*) (new StartCellWriteEvent(writer_queue, next_write_num)), sched_time);
        prev_dma_send_time = sched_time;

        // Clean up and record stats
        if (request_id == last_req_id) {
            // Last req in trace
            stats.delayEndStart = curTick() - trace_start_tick;
        }
        pending_req_t req = pending_reqs[request_id];
        ++stats.numDmaWrResp;
        stats.wrLatency.sample(curTick() - req.req_time);
        // Remove pending request
        pending_reqs.erase(request_id);
    }

    // ========= SpscDmaDevice =========

    class SpscDmaTaskEvent : Event
    {
        private:
        SpscDmaDevice* device;

        public:
        SpscDmaTaskEvent(SpscDmaDevice* dev) :
        Event(Default_Pri, AutoDelete), device(dev) {}

        void process() override {
            if (device->checkConsumer()) {
                device->runConsumer();
            } else {
                device->runProducer();
            }
        }
    };

    class SpscDmaConsumerEvent : Event
    {
        private:
        SpscDmaDevice* device;
        uint32_t completion_flag;

        public:
        SpscDmaConsumerEvent(SpscDmaDevice* dev, uint32_t flag) :
        Event(Default_Pri, AutoDelete), device(dev), completion_flag(flag) {}

        void process() override {
            device->update_sim_flags(completion_flag);
        }
    };

    SpscDmaDevice::SpscDmaDevice(const Params &p) : TestRdmaDevice(p) {
        pioSize = spsc_dma_reg_addr_map::SPSC_DMA_LAST_ADDR + 8;
        spsc_device_registers = new uint64_t [get_num_regs()];
        if (spsc_device_registers == NULL) {
            panic("Unable to allocate memory for DMA device registers.\n");
        }
        isConsumer = true;
        headvalue_var = (uint64_t*) device_memory;
        tailvalue_var = headvalue_var + 1;
        readvalue_start = tailvalue_var + 1;
    }

    SpscDmaDevice::~SpscDmaDevice() {
        TestRdmaDevice::~TestRdmaDevice();
    }

    Tick SpscDmaDevice::write(PacketPtr pkt) {
        Addr reg_addr = get_reg_addr(pkt->getAddr());
        uint64_t data = pkt->getLE<uint64_t>();

        switch(reg_addr) {
        case spsc_dma_reg_addr_map::SPSC_DMA_QUEUE_SIZE:
            // Assert that queue size is a power of 2
            // https://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2
            if ((data & (data - 1)) != 0) {
                panic("Queue size should be a power of 2");
            }
            spsc_device_registers[1] = data;
            break;
        case spsc_dma_reg_addr_map::SPSC_DMA_QUEUE_BASE:
            spsc_device_registers[2] = data;
            break;
        case spsc_dma_reg_addr_map::SPSC_DMA_QUEUE_HEAD:
            spsc_device_registers[3] = data;
            break;
        case spsc_dma_reg_addr_map::SPSC_DMA_QUEUE_TAIL:
            spsc_device_registers[4] = data;
            break;
        case spsc_dma_reg_addr_map::SPSC_DMA_CTRL_REG:
            spsc_device_registers[0] = data;
            isConsumer = ((data & SPSC_DMA_CTRL_REG_DIR) == SPSC_DMA_CTRL_REG_DIR);
            if (data & SPSC_DMA_CTRL_REG_START) {
                // Start consumer
                trace_start_tick = curTick() + pioDelay;
                SpscDmaTaskEvent* ev = new SpscDmaTaskEvent(this);
                schedule((Event*) ev, trace_start_tick);
            }
            break;
        default:
            panic("Write to address not supported on SpscDmaDevice");
            break;
        }

        pkt->makeTimingResponse();
        return pioDelay;
    }

    bool SpscDmaDevice::checkConsumer() {
        return isConsumer;
    }

    void SpscDmaDevice::runConsumer() {
        uint64_t num_elems = 0;
        uint64_t oldvalue = 0;
        uint32_t wait_dma_complete = 0;
        uint16_t counter = 0;
        uint16_t count_lim = spsc_device_registers[0] & SPSC_DMA_CTRL_REG_NMASK;

        Request::Flags dma_req_flag = Request::UNCACHEABLE;
        // In SPSC, only the consumer updates head
        Addr dma_addr = spsc_device_registers[3];
        int dma_size = sizeof(uint64_t);
        SpscDmaConsumerEvent* load_headvalue_ev = new SpscDmaConsumerEvent(this, SPSC_DMA_STATUS_LOAD_HEADVALUE);
        dma_status_register &= ~SPSC_DMA_STATUS_LOAD_HEADVALUE;
        dmaPort.dmaAction(MemCmd::ReadReq, dma_addr, dma_size, (Event*) load_headvalue_ev, (uint8_t*) headvalue_var, dmaDelay, dma_req_flag);

        while (counter < count_lim) {
            // Request tailvalue
            dma_addr = spsc_device_registers[4];
            int dma_size = sizeof(uint64_t);
            dma_req_flag = Request::UNCACHEABLE | Request::ACQUIRE;
            SpscDmaConsumerEvent* load_tailvalue_ev = new SpscDmaConsumerEvent(this, SPSC_DMA_STATUS_LOAD_TAILVALUE);
            dma_status_register &= ~SPSC_DMA_STATUS_LOAD_TAILVALUE;
            dmaPort.dmaAction(MemCmd::ReadReq, dma_addr, dma_size, (Event*) load_tailvalue_ev, (uint8_t*) tailvalue_var, 0, dma_req_flag);
            wait_dma_complete = SPSC_DMA_STATUS_LOAD_HEADVALUE | SPSC_DMA_STATUS_LOAD_TAILVALUE;

            // Wait for both headvalue and tailvalue dma to complete
            while ((dma_status_register & wait_dma_complete) != wait_dma_complete);

            // Number of elements to fetch
            num_elems = *tailvalue_var - *headvalue_var;
            if (num_elems > 0) {
                // This means that we can progress
                // We know queue size is a power of 2 so get headvalue % queue_size using (*headvalue_var & (spsc_device_registers[1] - 1))
                // Multiply index by 8 since each element in queue is 8 bytes to get address offset
                // Add address offset to base address of queue
                dma_addr = spsc_device_registers[2] + ((*headvalue_var & (spsc_device_registers[1] - 1)) << 3);
                int dma_size = sizeof(uint64_t) * num_elems;
                SpscDmaConsumerEvent* load_readvalue_ev = new SpscDmaConsumerEvent(this, SPSC_DMA_STATUS_LOAD_READVALUE);
                dma_status_register &= ~SPSC_DMA_STATUS_LOAD_READVALUE;
                dma_req_flag = Request::UNCACHEABLE;
                dmaPort.dmaAction(MemCmd::ReadReq, dma_addr, dma_size, (Event*) load_readvalue_ev, (uint8_t*) readvalue_start, 0, dma_req_flag);

                // Wait for readvalue dma to complete
                while ((dma_status_register & SPSC_DMA_STATUS_LOAD_READVALUE) != SPSC_DMA_STATUS_LOAD_READVALUE);

                // Verify data
                for (int i = 0; i < num_elems; ++i) {
                    if (readvalue_start[i] != (oldvalue + 1)) {
                        panic("Consumer Device Verification FAILURE %d %d", readvalue_start[i], oldvalue);
                    } else {
                        oldvalue = readvalue_start[i];
                        ++counter;
                        *headvalue_var = *headvalue_var + 1;
                    }
                }

                // Here we should have read everything
                if (*headvalue_var != *tailvalue_var) {
                    panic("Consumer Device indexing error. headvalue: %d, tailvalue: %d", *headvalue_var, *tailvalue_var);
                }

                // Update headvalue
                Addr dma_addr = spsc_device_registers[3];
                dma_size = sizeof(uint64_t);
                SpscDmaConsumerEvent* store_headvalue_ev = new SpscDmaConsumerEvent(this, SPSC_DMA_STATUS_STOR_HEADVALUE);
                dma_status_register &= ~SPSC_DMA_STATUS_STOR_HEADVALUE;
                dma_req_flag = Request::UNCACHEABLE | Request::RELEASE;
                dmaPort.dmaAction(MemCmd::WriteReq, dma_addr, dma_size, (Event*) store_headvalue_ev, (uint8_t*) headvalue_var, 0, dma_req_flag);
            }
        }
    }

    void SpscDmaDevice::runProducer() {}

    void SpscDmaDevice::update_sim_flags(uint32_t flag) {
        dma_status_register |= flag;
    }

    // ========= P2pTestDevice =========

    class P2pDmaCompletionEvent : Event
    {
        private:
        P2pWriteDevice* device;
        bool isRead;

        public:
        P2pDmaCompletionEvent(P2pWriteDevice* dev, bool isRd) :
        Event(Default_Pri, AutoDelete), device(dev), isRead(isRd) {}

        void process() override {
            device->handleDmaResponse(isRead);
        }
    };
    
    P2pWriteDevice::P2pWriteDevice(const Params &p) : MyDMADevice(p)
    {
        pioSize = p2pw_dma_reg_addr_map::P2PW_DMA_LAST_ADDR + 8;
        // device_registers incorrectly allocated by MyDMADevice
        delete device_registers;
        device_registers = new uint64_t[get_num_regs()];
        device_memory[0] = 0;
    }

    P2pWriteDevice::~P2pWriteDevice()
    {
        MyDMADevice::~MyDMADevice();
    }

    Tick P2pWriteDevice::write(PacketPtr pkt)
    {
        Addr reg_addr = get_reg_addr(pkt->getAddr());
        uint64_t data = pkt->getLE<uint64_t>();

        switch(reg_addr) {
        case p2pw_dma_reg_addr_map::P2PW_DMA_MEM_ADDR:
            device_registers[1] = data;
            break;
        case p2pw_dma_reg_addr_map::P2PW_DMA_MEM_SIZE:
            device_registers[2] = data;
            break;
        case p2pw_dma_reg_addr_map::P2PW_DMA_CTRL_REG:
            dmaPort.dmaAction(MemCmd::WriteReq, device_registers[1], 64, (Event*) new P2pDmaCompletionEvent(this, false), &(device_memory[0]), 0, 0);
            DPRINTF(P2pDevice, "Issuing P2P DMA write to address: %llx, data: %d\n", device_registers[1], device_memory[0]);
            break;
        default:
            panic("Write to address not supported on CellDevice");
            break;
        }

        pkt->makeTimingResponse();
        return pioDelay;
    }

    void P2pWriteDevice::handleDmaResponse(bool isRead)
    {
        if (isRead) {
            DPRINTF(P2pDevice, "P2P DMA read complete. Address: %llx, data: %d\n", device_registers[1], device_memory[0]);
            device_memory[0] = device_memory[0] + 1;
            // Issue next write
            DPRINTF(P2pDevice, "Issuing P2P DMA write to address: %llx, data: %d\n", device_registers[1], device_memory[0]);
            dmaPort.dmaAction(MemCmd::WriteReq, device_registers[1], 64, (Event*) new P2pDmaCompletionEvent(this, false), &(device_memory[0]), 0, 0);
        } else {
            DPRINTF(P2pDevice, "P2P DMA write complete. Address: %llx, data: %d\n", device_registers[1], device_memory[0]);
            DPRINTF(P2pDevice, "Issuing P2P DMA read to address: %llx\n", device_registers[1]);
            dmaPort.dmaAction(MemCmd::ReadReq, device_registers[1], 64, (Event*) new P2pDmaCompletionEvent(this, true), &(device_memory[0]), 0, 0);
        }
    }

    P2pReadDevice::P2pReadDevice(const Params &p) : MyDMADevice(p), 
    max_req_queue_size(p.req_queue_size), dmaReadPort(p.name + ".dma_read_port", *this)
    {
        pioSize = devMemSize;
    }

    P2pReadDevice::~P2pReadDevice()
    {
        MyDMADevice::~MyDMADevice();
    }

    void P2pReadDevice::init()
    {
        dmaReadPort.sendRangeChange();
        PioDevice::init();
    }

    AddrRangeList P2pReadDevice::getAddrRanges() const
    {
        AddrRangeList ranges;
        ranges.push_back(RangeSize(pioAddr, 8));
        return ranges;
    }

    AddrRangeList P2pReadDevice::getDmaReadRange()
    {
        assert(pioSize != 0);
        AddrRangeList ranges;
        ranges.push_back(RangeSize(pioAddr+8, pioSize-8));
        return ranges;
    }

    Tick P2pReadDevice::read(PacketPtr pkt)
    {
        Addr rel_addr = get_reg_addr(pkt->getAddr());

        pkt->makeTimingResponse();
        pkt->setData((const uint8_t*) &(device_memory[rel_addr]));
        DPRINTF(P2pDevice, "Read P2P device at address %llx\n", rel_addr);

        return pioDelay;
    }

    Tick P2pReadDevice::write(PacketPtr pkt)
    {
        Addr rel_addr = get_reg_addr(pkt->getAddr());

        memcpy(&(device_memory[rel_addr]), pkt->getConstPtr<uint8_t>(), pkt->getSize());

        pkt->makeTimingResponse();

        return pioDelay;
    }

    void P2pReadDevice::satisfy_read_req(PacketPtr pkt)
    {
        Tick delay = read(pkt);
        delay += pkt->headerDelay;
        pkt->headerDelay = 0;
        pkt->payloadDelay = 0;

        schedule((Event*) (new P2pSendReadRespEvent(pkt, this)), curTick() + delay);
    }

    void P2pReadDevice::recvTimingReq(PacketPtr pkt)
    {
        DPRINTF(P2pDevice, "P2P device req queue contains %d entries\n", req_queue.size());

        if (req_queue.empty()) {
            // Handle request
            satisfy_read_req(pkt);
        }

        // Enqueue request
        req_queue.push(pkt);
    }

    void P2pReadDevice::process_next_req()
    {
        if (!req_queue.empty()) {
            satisfy_read_req(req_queue.front());
        }
    }

    Port& P2pReadDevice::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "dma_read_port") {
            return dmaReadPort;
        } else if (if_name == "dma") {
            return MyDMADevice::getPort(if_name, idx);
        } else if (if_name == "pio") {
            return MyDMADevice::getPort(if_name, idx);
        } else {
            return ClockedObject::getPort(if_name, idx);
        }
    }

P2pReadDevice::DmaReadPort::DmaReadPort(const std::string& _name, P2pReadDevice& _p2pdev) : 
ResponsePort(_name, (PortID) 1), sendRetryEvent([this]{ processSendRetry(); }, _name), 
p2p_dev{_p2pdev}, blocked(false), response_blocked(false), mustSendRetry(false)
{}

void P2pReadDevice::DmaReadPort::processSendRetry()
{
    DPRINTF(P2pDevice, "DmaReadPort is sending retry\n");

    // reset the flag and call retry
    mustSendRetry = false;
    sendRetryReq();
}

bool P2pReadDevice::DmaReadPort::tryTiming(PacketPtr pkt)
{
    
    
    if (blocked || mustSendRetry) {
        mustSendRetry = true;
        return false;
    } else if (p2p_dev.req_queue_full()) {
        mustSendRetry = true;
        setBlocked();
        return false;
    } else {
        mustSendRetry = false;
        return true;
    }
}

bool P2pReadDevice::DmaReadPort::recvTimingReq(PacketPtr pkt)
{
    assert(pkt->isRequest());

    if (tryTiming(pkt)) {
        p2p_dev.recvTimingReq(pkt);
        return true;
    }

    return false;
}

void P2pReadDevice::DmaReadPort::recvRespRetry()
{
    response_blocked = false;
    while ((retry_resp_queue.size() > 0) && !response_blocked) {
        PacketPtr pkt = retry_resp_queue.front();
        sendResponse(pkt, true);
    }
}

void P2pReadDevice::DmaReadPort::setBlocked()
{
    assert(!blocked);

    blocked = true;
}

void P2pReadDevice::DmaReadPort::clearBlocked()
{
    assert(blocked);

    blocked = false;

    if (mustSendRetry) {
        p2p_dev.schedule(sendRetryEvent, curTick());
    }
}

void P2pReadDevice::DmaReadPort::sendResponse(PacketPtr pkt, bool isRetry)
{
    if (isRetry) {
        if (sendTimingResp(pkt)) {
            retry_resp_queue.erase(retry_resp_queue.begin());
            DPRINTF(P2pDevice, "%s: P2P Device retry for response packet %s successful\n", __func__, pkt->print());
        } else {
            response_blocked = true;
        }
    } else {
        if (response_blocked) {
            retry_resp_queue.push_back(pkt);
        } else if (!sendTimingResp(pkt)) {
            retry_resp_queue.push_back(pkt);
            response_blocked = true;
        } else {
            DPRINTF(P2pDevice, "%s: P2P Device sending response packet %s successful\n", __func__, pkt->print());
        }
    }
}

} // namespace gem5




