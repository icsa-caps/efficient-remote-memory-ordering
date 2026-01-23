#ifndef __MEM_REMOTE_LSQ_HH__
#define __MEM_REMOTE_LSQ_HH__

#include <cassert>
#include <cstdint>
#include <string>

#include <map>
#include <list>

#include "base/addr_range.hh"
#include "base/compiler.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "base/types.hh"

#include "mem/packet.hh"
#include "mem/packet_queue.hh"
#include "mem/qport.hh"
#include "mem/request.hh"

#include "params/RemoteLsq.hh"

#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "sim/probe/probe.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

namespace gem5 
{

typedef struct {
    PacketPtr req_packet;
    PacketPtr res_packet;
    bool complete;
} sim_rlsq_entry_t;

class RemoteLsq : public ClockedObject 
{
private:
    std::list<sim_rlsq_entry_t> queue;
    uint32_t queueSize;
    uint32_t numEntries;
    std::list<sim_rlsq_entry_t>::iterator last_sent;

    // Simulate accessing queue entry from PacketId
    // Think about how we can efficiently implement this in hw
    std::map<PacketId, std::list<sim_rlsq_entry_t>::iterator> queue_entry_map;

    bool allowMultiPendingReqs;
    bool simCorrectOrder;

    Cycles recv_lat;
    Cycles resp_lat;

    void updatePktDelay(PacketPtr pkt, Tick delay);
    void sendHead();

protected:
    // Port that should be connected to the xbar
    class BusSidePort : public QueuedResponsePort
    {
    private:
        void processSendRetry();

        EventFunctionWrapper sendRetryEvent;

    protected:
        RemoteLsq& rlsq;

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

        bool blocked;

        bool mustSendRetry;

        // Not snooping IOBus
        bool recvTimingSnoopResp(PacketPtr pkt) override { return false; };

        bool tryTiming(PacketPtr pkt) override;

        bool recvTimingReq(PacketPtr pkt) override;

        Tick recvAtomic(PacketPtr pkt) override;

        void recvFunctional(PacketPtr pkt) override;

        AddrRangeList getAddrRanges() const override { return rlsq.getAddrRanges(); };

    public:
        /** Do not accept any new requests. */
        void setBlocked();

        /** Return to normal operation and accept new requests. */
        void clearBlocked();

        bool isBlocked() const { return blocked; }

        BusSidePort(const std::string& _name, RemoteLsq& _rlsq, const std::string& _label);
    };

    // Port that should be connected to the IOCache
    class CacheSidePort : public RequestPort
    {
    private:

        RemoteLsq& rlsq;
        PacketPtr pending_req;
        bool blocked;

    protected:

        bool recvTimingResp(PacketPtr pkt) override
        {
            return rlsq.recvTimingResp(pkt);
        }

        void recvReqRetry() override;

        void recvRangeChange() override
        {
            rlsq.sendRangeChange();
        }

    public:

        CacheSidePort(const std::string &_name, RemoteLsq& _rlsq);
        void sendRequest(PacketPtr pkt);

        void setBlocked() { blocked = true; }
        bool isBlocked() { return blocked; }
        void clearBlocked();
    };

    BusSidePort busSidePort;
    CacheSidePort cacheSidePort;

    // Statistics
    struct RlsqStats : public statistics::Group {
        RlsqStats(statistics::Group *parent);
        statistics::Scalar numDmaWrReq;
        statistics::Scalar numDmaWrResp;
        statistics::Scalar totDmaWrBytes;
        statistics::Histogram wrLatency;
        statistics::Scalar numDmaRdReq;
        statistics::Scalar numDmaRdResp;
        statistics::Scalar totDmaRdBytes;
        statistics::Histogram rdLatency;
        statistics::Scalar blockedTicksNoEntries;
    } stats;

    std::map<PacketId, Tick> stat_req_issue_ticks;
    Tick blockedTick;

public:
    const AddrRangeList getAddrRanges() const { return cacheSidePort.getAddrRanges(); }

    PARAMS(RemoteLsq);
    RemoteLsq(const Params &p);
    ~RemoteLsq();

    void recvTimingReq(PacketPtr pkt);
    bool recvTimingResp(PacketPtr pkt);
    void sendRangeChange() { busSidePort.sendRangeChange(); }

    void sendNext();

    bool isFull() { return (numEntries >= queueSize); }

    Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
};

} // namespace gem5

#endif
