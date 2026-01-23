#ifndef __DEV_MYDMA_DEVICE_HH__
#define __DEV_MYDMA_DEVICE_HH__

#include <cstdlib>

#include "dev/dma_device.hh"
#include "params/MyDMADevice.hh"

#include "params/TestRdmaDevice.hh"
#include <queue>
#include <map>

#include "params/CellDevice.hh"
#include "params/SpscDmaDevice.hh"

#include "params/P2pWriteDevice.hh"
#include "params/P2pReadDevice.hh"

#include "base/statistics.hh"

namespace gem5
{

class MyDMADevice : public DmaDevice
{

private:
    Tick reqTime;

protected:
    Addr pioAddr;
    Addr pioSize;
    Tick pioDelay;

    Addr devMemSize;
    Addr nextDevAddr;
    Tick dmaDelay;

    Addr get_num_regs();
    Addr get_reg_addr(Addr paddr);

    uint64_t* device_registers;
    uint8_t* device_memory;

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    // Statistics
    struct MyDmaStats : public statistics::Group {
        MyDmaStats(statistics::Group *parent);
        statistics::Scalar numDmaWrReq;
        statistics::Scalar numDmaWrResp;
        statistics::Scalar totDmaWrBytes;
        statistics::Histogram wrLatency;
        statistics::Scalar numDmaRdReq;
        statistics::Scalar numDmaRdResp;
        statistics::Scalar totDmaRdBytes;
        statistics::Histogram rdLatency;
        statistics::Scalar delayEndStart;
        statistics::Formula avgDmaThroughput;
    } stats;

public:
    PARAMS(MyDMADevice);
    MyDMADevice(const Params &p);
    ~MyDMADevice();

    AddrRangeList getAddrRanges() const override;

    void maintainRespStatistics(PacketPtr packet);
};

typedef enum {
    RDMA_INV = 0,
    RDMA_RD,
    RDMA_WR
} rdma_op_t;

typedef struct {
    Addr start_addr;
    Addr req_size;
    uint8_t* data;
    rdma_op_t op;
    Request::Flags flags;
} rdma_req_t;

typedef struct {
    Tick req_time;
    Addr dev_addr;
    Addr req_size;
    uint8_t* data_ptr;
    rdma_op_t op;
} pending_req_t;

class TestRdmaDevice : public MyDMADevice
{
private:
    void print_trace();
    rdma_req_t read_trace_line(std::string line);
    void process_test_event();

    uint32_t num_qps;

protected:
    bool qpBlocking;
    bool clBlocking;
    std::queue<rdma_req_t> rdma_req_trace;
    EventFunctionWrapper initial_event;

    uint64_t cur_req_id;
    std::map<uint64_t, pending_req_t> pending_reqs;
    uint64_t last_req_id;

    Addr cpu_dma_base_addr;

    Tick trace_start_tick;

    void print_dma_req(rdma_req_t req);
    void send_dma_req(rdma_req_t req, Event* DmaCompletionEvent, uint8_t* read_dst);

public:
    PARAMS(TestRdmaDevice);
    TestRdmaDevice(const Params &p);
    ~TestRdmaDevice();

    Tick write(PacketPtr pkt) override;

    void send_trace_dma_req();
    void traceDmaResponse(uint64_t requestId);

    bool checkQpBlocking() { return qpBlocking; }
    bool checkClBlocking() { return clBlocking; }
    void split_dma_req(std::queue<rdma_req_t>*req_queue, rdma_req_t req);
};

class CellThread {
protected:
    CellDevice* dev;
    // Size of cell in bytes
    uint64_t cell_size;

    void dump_buffer(uint64_t* buf, uint64_t sz);

public:
    CellThread() : dev(NULL), cell_size(0) {}
    ~CellThread() {}
};

class CellReaderThread : public CellThread {
private:
    Addr src;
    uint64_t* tmp;

    uint32_t thread_id;
    uint64_t first_header;
    uint64_t second_header;

    std::queue<rdma_req_t> issue_queue;
    uint32_t tmp_index;

public:
    CellReaderThread();
    ~CellReaderThread();
    void init(CellDevice* devp, uint32_t tid, Addr srcv, uint64_t csize);
    void start_cell_read();
    void cell_version_read();
    bool handle_first_read(uint64_t* dst);
    uint8_t handle_second_read(uint64_t* dst);
    uint32_t get_num_remaining_cl() { return issue_queue.size(); }
    void issue_from_queue(bool isFirstCL);
    void copy_cache_line(uint64_t* dst);
    uint8_t handle_single_read(uint64_t* dst);
};

class CellWriterThread : public CellThread {
private:
    Addr dst;
    uint64_t* src;
    
    uint64_t version_num;

public:
    CellWriterThread();
    ~CellWriterThread();
    void init(CellDevice* devp, Addr dstv, uint64_t csize);
    void start_cell_write(uint8_t write_num);
    // void handle_write_completion(uint64_t request_id, uint8_t prev_write_num);
};

class CellDevice : public TestRdmaDevice {
private:
    bool use_writer;
    uint32_t num_readers;
    uint64_t max_cell_reads;
    uint64_t cur_cell_reads;
    uint64_t last_rd_req_id;
    CellReaderThread* queues;
    CellWriterThread* writer_queue;
    uint64_t* cell_device_registers;
    
    bool devMaxedReqs;
    Tick issue_latency;
    uint32_t batch_size;
    uint32_t batch_req_count;
    uint32_t batch_resp_count;
    Tick write_interval;
    std::vector<uint32_t> blocked_threads;

    std::queue<uint64_t*> receive_buffers;
    // For verifying that responses are received in order
    std::queue<uint64_t> req_id_queue;

    std::unordered_map<uint64_t,uint64_t*> unordered_receive_buffers;

    bool isSingleReq;

    bool issueP2P;
    Addr p2p_dev_addr;
    void send_p2p_dma(rdma_req_t req, Event* DmaCompletionEvent, uint8_t* read_dst);
    // Prevent multiple ports from scheduling send events in parallel
    Tick prev_dma_send_time;
    uint64_t completed_p2p_cell_rds;
    uint64_t p2p_cell_read_bytes;
    uint64_t tot_cell_read_bytes;
    Tick stat_last_issue_time;

protected:
    DmaPort p2pPort;

    struct CellStats : public statistics::Group {
        CellStats(statistics::Group *parent);
        statistics::Scalar numCellWrReq;
        statistics::Scalar numCellRdReq;
        statistics::Scalar totCellRdBytes;
        statistics::Scalar numTornCellRd;
        statistics::Scalar numSuccCellRd;
        statistics::Scalar numFailCellRd;
        statistics::Formula cellRdTornRate;
        statistics::Scalar cellRdDelayEndStart;
        statistics::Formula avgCellRdThroughput;
        statistics::Scalar numP2pCellRdIssued;
        statistics::Scalar numP2pCellRdCompleted;
        statistics::Scalar numCpuCellRd;
        statistics::Formula avgCellGetMops;
        statistics::Formula avgCpuCellGetMops;
        statistics::Scalar cpuCellRdBytes;
        statistics::Scalar p2pCellRdBytes;
        statistics::Formula cpuCellRdThroughput;
        statistics::Formula p2pCellRdThroughput;
        statistics::Histogram interIssueTime;
    } cell_stats;

public:
    PARAMS(CellDevice);
    CellDevice(const Params &p);
    ~CellDevice();

    Tick write(PacketPtr pkt) override;
    void send_cell_dma_rd_req(rdma_req_t req, uint32_t thread_id, uint8_t* dest, bool isVersionRead, bool isFirstCL);
    void send_cell_dma_wr_req(rdma_req_t req, uint8_t cur_write_num);
    void handle_read_completion(uint64_t request_id, uint32_t thread_id, bool isVersionRead);
    void handle_write_completion(uint64_t request_id, uint8_t prev_write_num);
    bool get_maxed_reqs() { return devMaxedReqs; }
    void set_maxed_reqs() { devMaxedReqs = true; }
    bool get_isSingleReq() { return isSingleReq; }

    Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
};

class SpscDmaDevice : public TestRdmaDevice
{
private:
    uint64_t* spsc_device_registers;
    bool isConsumer;

    // Bit positions for ctrl register (spsc_device_registers[0])
    const uint64_t SPSC_DMA_CTRL_REG_START = (1 << 20);
    const uint64_t SPSC_DMA_CTRL_REG_DIR   = (1 << 21);
    const uint64_t SPSC_DMA_CTRL_REG_NMASK = 0xFFFF;

    // Simplified tracking for which dma operations have completed
    uint32_t dma_status_register;
    const uint32_t SPSC_DMA_STATUS_LOAD_HEADVALUE = 1;
    const uint32_t SPSC_DMA_STATUS_LOAD_TAILVALUE = (1 << 1);
    const uint32_t SPSC_DMA_STATUS_LOAD_READVALUE = (1 << 2);
    const uint32_t SPSC_DMA_STATUS_STOR_HEADVALUE = (1 << 3);

    uint64_t* headvalue_var;
    uint64_t* tailvalue_var;
    uint64_t* readvalue_start;

public:
    PARAMS(SpscDmaDevice);
    SpscDmaDevice(const Params &p);
    ~SpscDmaDevice();

    Tick write(PacketPtr pkt) override;

    bool checkConsumer();
    void runConsumer();
    void runProducer();
    void update_sim_flags(uint32_t flag);
};

class P2pWriteDevice : public MyDMADevice {
private:
    
protected:
    Tick write(PacketPtr pkt) override;

public:
    PARAMS(P2pWriteDevice);    
    P2pWriteDevice(const Params &p);
    ~P2pWriteDevice();

    void handleDmaResponse(bool isRead);
};

class P2pReadDevice : public MyDMADevice {
private:
    std::queue<PacketPtr> req_queue;
    uint64_t max_req_queue_size;

    void satisfy_read_req(PacketPtr pkt);

protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    class DmaReadPort : public ResponsePort
    {
    private:
        std::vector<PacketPtr> retry_resp_queue;

        void processSendRetry();

        EventFunctionWrapper sendRetryEvent;

    protected:
        P2pReadDevice& p2p_dev;

        bool blocked;
        bool response_blocked;

        bool mustSendRetry;

        // Not snooping IOBus
        bool recvTimingSnoopResp(PacketPtr pkt) override { return false; };

        bool tryTiming(PacketPtr pkt) override;

        bool recvTimingReq(PacketPtr pkt) override;

        void recvRespRetry() override;

        Tick recvAtomic(PacketPtr pkt) override
        {
            // Atomic simulation mode not supported.
            assert(false);
            return 0;
        }

        void recvFunctional(PacketPtr pkt) override
        {
            // Functional simulation mode not supported.
            assert(false);
        }

        AddrRangeList getAddrRanges() const override { return p2p_dev.getDmaReadRange(); }

    public:
        /** Do not accept any new requests. */
        void setBlocked();

        /** Return to normal operation and accept new requests. */
        void clearBlocked();

        bool isBlocked() const { return blocked; }

        DmaReadPort(const std::string& _name, P2pReadDevice& _p2pdev);

        void sendResponse(PacketPtr, bool isRetry);
    };

    DmaReadPort dmaReadPort;

public:
    PARAMS(P2pReadDevice);   
    P2pReadDevice(const Params &p);
    ~P2pReadDevice();

    void init() override;

    AddrRangeList getAddrRanges() const override;
    AddrRangeList getDmaReadRange();

    void recvTimingReq(PacketPtr pkt);
    bool req_queue_full() { return req_queue.size() == max_req_queue_size; }
    void DmaReadPortSendResp(PacketPtr pkt) { 
        dmaReadPort.sendResponse(pkt, false); 
        req_queue.pop();
        if (!req_queue_full() && dmaReadPort.isBlocked()) {
            dmaReadPort.clearBlocked();
        }
        process_next_req();
    }
    void process_next_req();
    Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
};

class P2pSendReadRespEvent : Event
{
private:
    PacketPtr pkt;
    P2pReadDevice* p2p_dev;

public:
    P2pSendReadRespEvent(PacketPtr p, P2pReadDevice* p2pd) : Event(Default_Pri, AutoDelete), 
    pkt(p), p2p_dev(p2pd) {}
    void process() override {
        p2p_dev->DmaReadPortSendResp(pkt);
    }

};

} // namespace gem5

#endif // __DEV_MYDMA_DEVICE_HH__




