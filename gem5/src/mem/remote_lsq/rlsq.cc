#include "rlsq.hh"
#include "debug/RemoteLsq.hh"

namespace gem5
{

RemoteLsq::RemoteLsq(const Params &p) : ClockedObject(p),
busSidePort(p.name + ".bus_side_port", *this, "BusSidePort"), 
cacheSidePort(p.name + ".cache_side_port", *this), stats(this)
{
    queueSize = p.queue_size;
    numEntries = 0;
    allowMultiPendingReqs = p.allow_multiple_pending_reqs;
    simCorrectOrder = p.sim_correct_order;
    recv_lat = p.push_lat;
    resp_lat = p.pop_lat;
    blockedTick = 0;
}

RemoteLsq::~RemoteLsq()
{}

RemoteLsq::RlsqStats::RlsqStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(numDmaWrReq, statistics::units::Count::get(), "Number of DMA Write Requests"),
      ADD_STAT(numDmaWrResp, statistics::units::Count::get(), "Number of DMA Write Responses"),
      ADD_STAT(totDmaWrBytes, statistics::units::Byte::get(), "Total number of bytes transferred by DMA Writes"),
      ADD_STAT(wrLatency, statistics::units::Tick::get(), "Ticks between write req and write resp"),
      ADD_STAT(numDmaRdReq, statistics::units::Count::get(), "Number of DMA Read Requests"),
      ADD_STAT(numDmaRdResp, statistics::units::Count::get(), "Number of DMA Read Responses"),
      ADD_STAT(totDmaRdBytes, statistics::units::Byte::get(), "Total number of bytes transferred by DMA Reads"),
      ADD_STAT(rdLatency, statistics::units::Tick::get(), "Ticks between read req and read resp"),
      ADD_STAT(blockedTicksNoEntries, statistics::units::Tick::get(), "Total ticks stalled due to no LSQ entries")
{
    wrLatency.init(16); // number of buckets
    rdLatency.init(16); // number of buckets
}

void RemoteLsq::updatePktDelay(PacketPtr pkt, Tick delay)
{
    // Can be called at a time that is not necessarily
    // coinciding with its own clock, so start by determining how long
    // until the next clock edge (could be zero)
    Tick offset = clockEdge() - curTick();

    pkt->headerDelay += offset + delay;
}

void RemoteLsq::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(RemoteLsq, "%s: Handling request %s\n", __func__, pkt->print());

    queue.push_back({pkt, nullptr, false});
    std::list<sim_rlsq_entry_t>::iterator it = queue.end();
    queue_entry_map.insert(std::pair<PacketId, std::list<sim_rlsq_entry_t>::iterator>(pkt->id, --it));
    ++numEntries;
    if (isFull()) {
        busSidePort.setBlocked();
        blockedTick = curTick();
    }

    if (allowMultiPendingReqs) {
        // Send packet to cache, assuming that reqs arrived in order that they are issued by device 
        DPRINTF(RemoteLsq, "Trying to forward to cache. Number of entries in queue:%d\n", numEntries);
        if (simCorrectOrder) {
            if (!cacheSidePort.isBlocked()) {
                cacheSidePort.sendRequest(pkt);
                last_sent = it;
            }
        } else {
            if ((numEntries == 1) && (!cacheSidePort.isBlocked())) {
                sendHead();
            }
        }
    } else {
        // Simple model
        if (numEntries == 1) {
            updatePktDelay(pkt, cyclesToTicks(recv_lat));
            DPRINTF(RemoteLsq, "Current Tick: %lld, latency: %lld. Packet header delay: %lld\n", 
                curTick(), cyclesToTicks(recv_lat), pkt->headerDelay);
            cacheSidePort.sendRequest(pkt);
        }
    }

    // Maintain stats
    if (pkt->isWrite()) {
        ++stats.numDmaWrReq;
        stats.totDmaWrBytes += pkt->getSize();
    } else if (pkt->isRead()) {
        ++stats.numDmaRdReq;
        stats.totDmaRdBytes += pkt->getSize();
    }

    stat_req_issue_ticks.insert(std::pair<PacketId, Tick>(pkt->id, curTick()));
}

bool RemoteLsq::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(RemoteLsq, "%s: Handling response %s\n", __func__, pkt->print());
    
    // Latency to manage queue for response (only for read req)
    Tick resp_latency;
    
    if (allowMultiPendingReqs) {
        if (simCorrectOrder) {
            // Update queue entry
            // Check if response is created from request packet
            assert(queue_entry_map.find(pkt->id) != queue_entry_map.end());
            std::list<sim_rlsq_entry_t>::iterator entry_ptr = queue_entry_map[pkt->id];
            entry_ptr->res_packet = pkt;
            entry_ptr->complete = true;

            // Send out responses in-order
            while ((numEntries > 0) && ((queue.begin())->complete)) {
                entry_ptr = queue.begin();
                resp_latency = (entry_ptr->res_packet->isRead()) ? cyclesToTicks(resp_lat) : 0;
                busSidePort.schedTimingResp(entry_ptr->res_packet, curTick() + resp_latency);
                DPRINTF(RemoteLsq, "Response scheduled for bus\n");
                queue_entry_map.erase((entry_ptr->req_packet)->id);
                queue.pop_front();
                --numEntries;
                if (busSidePort.isBlocked() && !isFull()) {
                    busSidePort.clearBlocked();
                    stats.blockedTicksNoEntries += curTick() - blockedTick;
                }
            }
        } else {
            resp_latency = (pkt->isRead()) ? cyclesToTicks(resp_lat) : 0;
            busSidePort.schedTimingResp(pkt, curTick() + resp_latency);
            DPRINTF(RemoteLsq, "Response scheduled for bus\n");
        }
    } else {
        resp_latency = (pkt->isRead()) ? cyclesToTicks(resp_lat) : 0;
        // Simple model
        // Place response on PCIe link
        DPRINTF(RemoteLsq, "Current Tick: %lld, latency: %lld. Scheduling response at %lld\n", 
            curTick(), resp_latency, curTick() + resp_latency);
        busSidePort.schedTimingResp(pkt, curTick() + resp_latency);

        // Remove request from lsq
        queue_entry_map.erase(((queue.begin())->req_packet)->id);
        queue.pop_front();
        --numEntries;
        if (busSidePort.isBlocked() && !isFull()) {
            busSidePort.clearBlocked();
            stats.blockedTicksNoEntries += curTick() - blockedTick;
        }

        // Send next request (if exists) to cache
        if (numEntries > 0) {
            PacketPtr send_pkt = (queue.begin())->req_packet;
            updatePktDelay(send_pkt, cyclesToTicks(recv_lat));
            DPRINTF(RemoteLsq, "Current Tick: %lld, latency: %lld. Packet header delay: %lld\n", 
                curTick(), cyclesToTicks(recv_lat), pkt->headerDelay);
            cacheSidePort.sendRequest(send_pkt);
        }
    }

    // Update stats
    Tick lat = curTick() - stat_req_issue_ticks[pkt->id];
    if (pkt->isWrite()) {
        ++stats.numDmaWrResp;
        stats.wrLatency.sample(lat);
    } else if (pkt->isRead()) {
        ++stats.numDmaRdResp;
        stats.rdLatency.sample(lat);
    }
    stat_req_issue_ticks.erase(pkt->id);

    return true;
}

// For testing no ordering
void RemoteLsq::sendHead() {
    if ((numEntries > 0) && (!cacheSidePort.isBlocked())) {
        DPRINTF(RemoteLsq, "Calling sendHead, number of entries in queue:%d\n", numEntries);
        cacheSidePort.sendRequest((queue.begin())->req_packet);
        queue_entry_map.erase(((queue.begin())->req_packet)->id);
        queue.pop_front();
        --numEntries;
        if (busSidePort.isBlocked() && !isFull()) {
            busSidePort.clearBlocked();
            stats.blockedTicksNoEntries += curTick() - blockedTick;
        }
        sendHead();
    }
}

// Called when cacheSidePort block is cleared
void RemoteLsq::sendNext() {
    if (simCorrectOrder){
        std::list<sim_rlsq_entry_t>::iterator temp = last_sent;
        ++last_sent;
        while ((last_sent != queue.end()) && (!cacheSidePort.isBlocked())) {
            temp = last_sent;
            cacheSidePort.sendRequest(last_sent->req_packet);
            ++last_sent;
        }
        last_sent = temp;
    } else {
        sendHead();
    }
}

Port &RemoteLsq::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "bus_side_port") {
        return busSidePort;
    } else if (if_name == "cache_side_port") {
        return cacheSidePort;
    }  else {
        return ClockedObject::getPort(if_name, idx);
    }
}

RemoteLsq::BusSidePort::BusSidePort(const std::string& _name, RemoteLsq& _rlsq, const std::string& _label) : 
QueuedResponsePort(_name, queue), sendRetryEvent([this]{ processSendRetry(); }, _name), 
rlsq{_rlsq}, queue(rlsq, *this, true, _label), blocked(false), mustSendRetry(false)
{}

void RemoteLsq::BusSidePort::processSendRetry()
{
    DPRINTF(RemoteLsq, "BusSidePort is sending retry\n");

    // reset the flag and call retry
    mustSendRetry = false;
    sendRetryReq();
}

bool RemoteLsq::BusSidePort::tryTiming(PacketPtr pkt)
{
    if (blocked || mustSendRetry) {
        mustSendRetry = true;
        return false;
    } else {
        mustSendRetry = false;
        return true;
    }
}

bool RemoteLsq::BusSidePort::recvTimingReq(PacketPtr pkt)
{
    assert(pkt->isRequest());

    if (tryTiming(pkt)) {
        rlsq.recvTimingReq(pkt);
        return true;
    }

    return false;
}

Tick RemoteLsq::BusSidePort::recvAtomic(PacketPtr pkt)
{
    // Atomic simulation mode not supported.
    assert(false);
    return 0; 
}

void RemoteLsq::BusSidePort::recvFunctional(PacketPtr pkt)
{
    // Functional simulation mode not supported.
    assert(false);
}

void RemoteLsq::BusSidePort::setBlocked()
{
    assert(!blocked);

    blocked = true;
}

void RemoteLsq::BusSidePort::clearBlocked()
{
    assert(blocked);

    blocked = false;

    if (mustSendRetry) {
        rlsq.schedule(sendRetryEvent, curTick());
    }
}

RemoteLsq::CacheSidePort::CacheSidePort(const std::string &_name, RemoteLsq& _rlsq) : RequestPort(_name, (PortID) 0), rlsq(_rlsq)
{
    blocked = false;
    pending_req = nullptr;
}

void RemoteLsq::CacheSidePort::sendRequest(PacketPtr pkt)
{
    panic_if(pending_req != nullptr, "Should never try to send if blocked!");
    if (!sendTimingReq(pkt)) {
        DPRINTF(RemoteLsq, "%s fail, blocking packet:%s\n", __func__, pkt->print());
        pending_req = pkt;
        setBlocked();
    } else {
        DPRINTF(RemoteLsq, "%s success, packet:%s\n", __func__, pkt->print());
        if (isBlocked()) {
            DPRINTF(RemoteLsq, "%s unblocking port\n", __func__);
            clearBlocked();
        }
    }
}

void RemoteLsq::CacheSidePort::recvReqRetry()
{
    assert(pending_req != nullptr);

    PacketPtr pkt = pending_req;
    pending_req = nullptr;

    sendRequest(pkt);
}

void RemoteLsq::CacheSidePort::clearBlocked()
{
    assert(blocked);

    blocked = false;
    pending_req = nullptr;
    
    rlsq.sendNext();
}

} // namespace gem5


