#ifndef __MEM_IOHUB_HH__
#define __MEM_IOHUB_HH__

#include <cassert>
#include <cstdint>
#include <string>

#include <unordered_map>
#include <queue>
#include <vector>

#include "base/addr_range.hh"
#include "base/compiler.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "base/types.hh"

#include "mem/packet.hh"
#include "mem/packet_queue.hh"
#include "mem/qport.hh"
#include "mem/request.hh"

#include "params/IOHub.hh"

#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "sim/probe/probe.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

namespace gem5 
{

class IOHubQueueEntry : public Packet::SenderState
{
private:
    // For writes, a list of reads that are ordered after it
    // For reads, a list of writes that cannot be issued because we are waiting for a GetS
    std::queue<IOHubQueueEntry*> waitlist;
    const PacketPtr pkt;
    // Response packet for reads
    PacketPtr resp_pkt;
    const Tick forward_time;
    Tick response_time;
    bool inService;
    bool isComplete;
    bool isCommitted;
    // For debugging
    bool isInvalidated;
    bool isWaiting;

public:
    IOHubQueueEntry(PacketPtr p, Tick t) : pkt(p), resp_pkt(NULL), 
    forward_time(t), response_time(0), inService(false), isComplete(false), isCommitted(false), 
    isInvalidated(false) {}
    void add_waiting_req(IOHubQueueEntry* read_entry) { waitlist.push(read_entry); }
    PacketPtr get_packet_ptr() { return (isCommitted ? NULL : pkt); }
    Tick get_forward_time() { return forward_time; }
    bool getInService() { return inService; }
    void setInService() { inService = true; }
    void clearInService() { inService = false; }
    bool getIsComplete() { return isComplete; }
    void setIsComplete() { isComplete = true; }
    void clearIsComplete() { isComplete = false; }
    bool getIsCommitted() { return isCommitted; }
    void setIsCommitted() { isCommitted = true; }
    void send_waiting_reqs(std::queue<IOHubQueueEntry*>* out_queue);
    void set_resp_packet(PacketPtr rp) { resp_pkt = rp; }
    PacketPtr get_resp_packet() { assert(resp_pkt != NULL); return resp_pkt; }
    void set_response_time(Tick t) { response_time = t; }
    Tick get_response_time() {return response_time; }
    PacketPtr applyReadResponse();
    bool getInvalidated() { return isInvalidated; }
    void setInvalidated() { isInvalidated = true; }
    void clearInvalidated() { isInvalidated = false; }
    bool getIsWaiting() { return isWaiting; }
    void setIsWaiting() { isWaiting = true; }
    void clearIsWaiting() { isWaiting = false; }
};

typedef struct {
    std::vector<IOHubQueueEntry*> outstanding_reqs;
    bool hasOutstandingWriteReq;
} tracker_entry_t;

class IOHub : public ClockedObject 
{
private:
    // Receive queue from PCIe
    std::queue<IOHubQueueEntry*> ls_queue;
    const uint32_t lsq_size;
    std::queue<IOHubQueueEntry*> squeue;
    uint32_t num_used_queue_entries;

    // Check for cache line conflicts
    std::unordered_map<Addr,tracker_entry_t> cache_line_trackers;
    const uint32_t num_trackers;
    uint32_t used_trackers;

    // For invalidation tracking
    std::unordered_map<Addr,std::vector<IOHubQueueEntry*>> speculating_reads;
    std::unordered_map<Addr,std::vector<IOHubQueueEntry*>> permitted_writes;

    // Allocate a cache block on a line-sized write miss
    const bool doFastWrites;

    bool allow_multiple_pending_reqs;
    bool read_inorder;
    bool resp_inorder;

    bool use_speculative_reads() { return (allow_multiple_pending_reqs && read_inorder); }
    bool use_blocking_ordered_reads() { return (!allow_multiple_pending_reqs && read_inorder); }
    bool use_baseline_ioh() { return (allow_multiple_pending_reqs && !read_inorder); }
    // !allow_multiple_pending_reqs && !read_inorder : invalid configuration

    unsigned blkSize;

    /**
     * The latency of tag lookup of a cache. It occurs when there is
     * an access to the cache.
     */
    Cycles lookupLatency;

    /**
     * The latency of data access of a cache. It occurs when there is
     * an access to the cache.
     */
    Cycles dataLatency;

    /**
     * This is the forward latency of the cache. It occurs when there
     * is a cache miss and a request is forwarded downstream, in
     * particular an outbound miss.
     */
    Cycles forwardLatency;

    /** The latency to fill a cache block */
    Cycles fillLatency;

    /**
     * The latency of sending reponse to its upper level cache/core on
     * a linefill. The responseLatency parameter captures this
     * latency.
     */
    Cycles responseLatency;

    IOHubQueueEntry* latest_write;

    // Use this in the case of blocking, ordered reads
    std::queue<IOHubQueueEntry*> lqueue;

    Tick last_response_time;

protected:
    // Port that should be connected to the xbar
    class DevSidePort : public ResponsePort
    {
    private:
        std::vector<PacketPtr> retry_resp_queue;

        void processSendRetry();

        EventFunctionWrapper sendRetryEvent;

    protected:
        IOHub& iohub;

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

        AddrRangeList getAddrRanges() const override { return iohub.getAddrRanges(); };

    public:
        /** Do not accept any new requests. */
        void setBlocked();

        /** Return to normal operation and accept new requests. */
        void clearBlocked();

        bool isBlocked() const { return blocked; }

        DevSidePort(const std::string& _name, IOHub& _iohub);

        void sendResponse(PacketPtr, bool isRetry);
    };

    // Port that should be connected to the IOCache
    class MemSidePort : public RequestPort
    {
    private:

        IOHub& iohub;
        std::queue<PacketPtr> pending_req_queue;
        bool blocked;

    protected:

        bool recvTimingResp(PacketPtr pkt) override
        {
            return iohub.recvTimingResp(pkt);
        }

        void recvReqRetry() override;

        void recvTimingSnoopReq(PacketPtr pkt) override;

        void recvFunctionalSnoop(PacketPtr pkt) override { recvTimingSnoopReq(pkt); }

        void recvRangeChange() override { iohub.sendRangeChange(); }

    public:

        MemSidePort(const std::string &_name, IOHub& _iohub);
        void sendRequest(PacketPtr pkt, bool isRetry);

        void setBlocked() { blocked = true; }
        bool isBlocked() { return blocked; }
        void clearBlocked();

        bool isSnooping() const override { return true; }
    };

    DevSidePort devSidePort;
    MemSidePort memSidePort;

    void promoteWholeLineWrites(PacketPtr pkt);
    void recvTimingSnoopReq(PacketPtr pkt);
    bool isLatestWrite(IOHubQueueEntry* write_entry_ptr);
    void allocateTracker(PacketPtr pkt, IOHubQueueEntry* queue_entry_ptr, Addr blk_addr);
    void issueMemPacket(PacketPtr pkt, IOHubQueueEntry* queue_entry_ptr);
    PacketPtr createMissPacket(PacketPtr dev_pkt, bool needs_writable, bool is_whole_line_write);
    bool checkTrackerConflict(IOHubQueueEntry* queue_entry_ptr, bool* canIssue, Addr blk_addr);
    void cleanLSQ();

    // Statistics
    struct IOHubStats : public statistics::Group {
        IOHubStats(statistics::Group *parent);
        statistics::Scalar numInvalidatedRds;
        statistics::Scalar numInvalidatedWrs;
    } stats;

public:
    IOHub(const IOHubParams &p);

    const AddrRangeList getAddrRanges() const { return memSidePort.getAddrRanges(); }
    bool inRange(Addr addr);

    void recvTimingReq(PacketPtr pkt);
    bool recvTimingResp(PacketPtr pkt);
    void sendRangeChange() { devSidePort.sendRangeChange(); }

    void memSidePortSendReq(PacketPtr pkt);
    void devSidePortSendResp(PacketPtr pkt);

    bool lsqFull();
    bool noTrackers();

    Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
};

class IOHubSendMemReqEvent : Event
{
private:
    PacketPtr pkt;
    IOHub* iohub;

public:
    IOHubSendMemReqEvent(PacketPtr p, IOHub* ioh) : Event(Default_Pri, AutoDelete), 
    pkt(p), iohub(ioh) {}
    void process() override {
        iohub->memSidePortSendReq(pkt);
    }

};

class IOHubSendReadRespEvent : Event
{
private:
    PacketPtr pkt;
    IOHub* iohub;

public:
    IOHubSendReadRespEvent(PacketPtr p, IOHub* ioh) : Event(Default_Pri, AutoDelete), 
    pkt(p), iohub(ioh) {}
    void process() override {
        iohub->devSidePortSendResp(pkt);
    }

};

} // NAMESPACE GEM5

#endif // __MEM_IOHUB_HH__
