#include <cassert>

#include "iohub.hh"
#include "debug/IOHub.hh"
#include "debug/IOHubVerbose.hh"

namespace gem5 
{

void IOHubQueueEntry::send_waiting_reqs(std::queue<IOHubQueueEntry*>* out_queue)
{
    while (!waitlist.empty()) {
        out_queue->push(waitlist.front());
        waitlist.pop();
    }
}

PacketPtr IOHubQueueEntry::applyReadResponse()
{
    // Copy data from response
    pkt->setData(resp_pkt->getConstPtr<uint8_t>());
    pkt->copyResponderFlags(resp_pkt);
    pkt->makeTimingResponse();
    return pkt;
}

IOHub::IOHubStats::IOHubStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(numInvalidatedRds, statistics::units::Count::get(), "Number of Invalidated Reads"),
      ADD_STAT(numInvalidatedWrs, statistics::units::Count::get(), "Number of Invalidated Writes")
{}

IOHub::IOHub(const IOHubParams &p) : ClockedObject(p),
lsq_size(p.queue_size), num_used_queue_entries(0), 
num_trackers(p.num_trackers), used_trackers(0), 
doFastWrites(p.do_fast_writes), 
devSidePort(p.name + ".dev_side_port", *this), 
memSidePort(p.name + ".mem_side_port", *this), stats(this)
{
    allow_multiple_pending_reqs = p.allow_multiple_pending_reqs;
    read_inorder = p.read_inorder;
    resp_inorder = p.resp_inorder;
    assert(allow_multiple_pending_reqs || read_inorder);

    blkSize = p.system->cacheLineSize();
    lookupLatency = p.tag_latency;
    dataLatency = p.data_latency;
    forwardLatency = p.tag_latency;
    fillLatency = p.data_latency;
    responseLatency = p.response_latency;

    latest_write = NULL;

    last_response_time = 0;
}

void IOHub::promoteWholeLineWrites(PacketPtr pkt)
{
    // Cache line clearing instructions
    if (doFastWrites && (pkt->cmd == MemCmd::WriteReq) &&
        (pkt->getSize() == blkSize) && (pkt->getOffset(blkSize) == 0) &&
        !pkt->isMaskedWrite()) {
        pkt->cmd = MemCmd::WriteLineReq;
        DPRINTF(IOHub, "packet promoted from Write to WriteLineReq\n");
    }
}

bool IOHub::inRange(Addr addr)
{
    for (const auto& r : getAddrRanges()) {
        if (r.contains(addr)) {
            return true;
       }
    }
    return false;
}

void IOHub::recvTimingSnoopReq(PacketPtr pkt)
{
    // no need to snoop requests that are not in range
    if (!inRange(pkt->getAddr())) {
        DPRINTF(IOHubVerbose, "Target address %llx for snoop not in IOHub range\n", pkt->getAddr());
        return;
    }

    // Need to check if snoop hits a valid write that is pending writeback
    Addr snoop_blk_addr = pkt->getBlockAddr(blkSize);
    std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator write_tracker = permitted_writes.find(snoop_blk_addr);
    std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator spec_tracker = speculating_reads.find(snoop_blk_addr);
    bool snoop_hit = (write_tracker != permitted_writes.end()) || (spec_tracker != speculating_reads.end());
    if (!snoop_hit) {
        DPRINTF(IOHubVerbose, "Target block %llx for snoop not in IOHub\n", snoop_blk_addr);
        return;
    }

    bool snooped_invalidate = pkt->isInvalidate();
    bool snooped_read = pkt->isRead();

    DPRINTF(IOHub, "%s: IOHub snooped packet %s, with blk_addr %llx, invalidate:%d, read:%d\n", 
        __func__, pkt->print(), snoop_blk_addr,snooped_invalidate, snooped_read);

    // Invalidate speculative reads
    if (use_speculative_reads() && snooped_invalidate && (spec_tracker != speculating_reads.end())) {
        DPRINTF(IOHub, "%s: IOHub invalidating reads for block address %llx\n", __func__, snoop_blk_addr);
        std::vector<IOHubQueueEntry*>* invalidated_reads = &(spec_tracker->second);
        while (!invalidated_reads->empty()) {
            IOHubQueueEntry* read_entry = invalidated_reads->front();
            assert(read_entry->get_packet_ptr()->isRead());
            read_entry->clearInService();
            read_entry->clearIsComplete();
            read_entry->setInvalidated();
            read_entry->set_resp_packet(NULL);
            invalidated_reads->erase(invalidated_reads->begin());
            DPRINTF(IOHub, "%s: IOHub invalidated read packet %s\n", __func__, read_entry->get_packet_ptr()->print());
            ++stats.numInvalidatedRds;
        }
        assert(invalidated_reads->empty());
        speculating_reads.erase(spec_tracker);
    }

    if (snooped_read && (write_tracker != permitted_writes.end())) {
        // Need to check if write can be committed
        // We should use a separate SQ to allow writes to pass reads.
        DPRINTF(IOHub, "%s: Checking snooped writes for block address %llx\n", __func__, snoop_blk_addr);
        std::vector<IOHubQueueEntry*>* completed_writes = &(write_tracker->second);
        while (!completed_writes->empty()) {
            IOHubQueueEntry* write_entry = completed_writes->front();
            if (write_entry == squeue.front()) {
                // Write is ready to be committed
                assert(!write_entry->getIsCommitted());
                PacketPtr write_pkt = write_entry->get_packet_ptr();
                assert(write_pkt->isWrite());
                // Commit write by responding to read request
                pkt->setCacheResponding();
                pkt->setResponderHadWritable();
                PacketPtr resp_pkt = new Packet(pkt, false, true);
                resp_pkt->setData(write_pkt->getConstPtr<uint8_t>());
                resp_pkt->makeTimingResponse();
                Tick respond_time = clockEdge(forwardLatency) + pkt->headerDelay;
                // Here we reset the timing of the packet.
                resp_pkt->headerDelay = resp_pkt->payloadDelay = 0;
                DPRINTF(IOHub, "%s: created response: %s tick: %lu\n", __func__, resp_pkt->print(), respond_time);
                schedule((Event*)(new IOHubSendMemReqEvent(resp_pkt, this)), respond_time);
                // Clean store queue and trackers
                squeue.pop();
                --num_used_queue_entries;
                completed_writes->erase(completed_writes->begin());
                DPRINTF(IOHub, "%s: IOHub committed write packet %s\n", __func__, write_entry->get_packet_ptr()->print());
                write_entry->setIsCommitted();
            }
        }
    }

    // Invalidate permitted writes
    if (snooped_invalidate && (write_tracker != permitted_writes.end())) {
        DPRINTF(IOHub, "%s: IOHub invalidating writes for block address %llx\n", __func__, snoop_blk_addr);
        std::vector<IOHubQueueEntry*>* invalidated_writes = &(write_tracker->second);
        while (!invalidated_writes->empty()) {
            IOHubQueueEntry* write_entry = invalidated_writes->front();
            assert(!write_entry->getIsCommitted());
            assert(write_entry->get_packet_ptr()->isWrite());
            write_entry->clearInService();
            write_entry->clearIsComplete();
            write_entry->setInvalidated();
            invalidated_writes->erase(invalidated_writes->begin());
            DPRINTF(IOHub, "%s: IOHub invalidated write packet %s\n", __func__, write_entry->get_packet_ptr()->print());
            ++stats.numInvalidatedWrs;
        }
        assert(invalidated_writes->empty());
        permitted_writes.erase(write_tracker);
    }

    // Update the latency cost of the snoop so that the crossbar can
    // account for it. Do not overwrite what other neighbouring caches
    // have already done, rather take the maximum. The update is
    // tentative, for cases where we return before an upward snoop
    // happens below.
    pkt->snoopDelay = std::max<uint32_t>(pkt->snoopDelay, lookupLatency * clockPeriod());
}

PacketPtr IOHub::createMissPacket(PacketPtr dev_pkt, bool needs_writable, bool is_whole_line_write)
{
    assert(dev_pkt->needsResponse());

    MemCmd cmd;
    if (is_whole_line_write) {
        cmd = MemCmd::InvalidateReq;
    } else {
        // If we do not need a writable copy of the cache line, request a clean copy
        cmd = needs_writable ? MemCmd::ReadExReq : MemCmd::ReadCleanReq;
    }
    PacketPtr pkt = new Packet(dev_pkt->req, cmd, blkSize);

    // if there are upstream caches that have already marked the
    // packet as having sharers (not passing writable), pass that info
    // downstream
    if (dev_pkt->hasSharers() && !needs_writable) {
        pkt->setHasSharers();
        DPRINTF(IOHub, "%s: passing hasSharers from %s to %s\n", __func__, dev_pkt->print(), pkt->print());
    }

    // the packet should be block aligned
    assert(pkt->getAddr() == pkt->getBlockAddr(blkSize));

    pkt->allocate();
    DPRINTF(IOHub, "%s: created %s from %s\n", __func__, pkt->print(), dev_pkt->print());

    return pkt;
}

void IOHub::allocateTracker(PacketPtr pkt, IOHubQueueEntry* queue_entry_ptr, Addr blk_addr)
{   
    std::unordered_map<Addr,tracker_entry_t>::iterator pos = cache_line_trackers.find(blk_addr);
    if (pos == cache_line_trackers.end()) {
        // Allocate tracker
        Addr blk_addr = pkt->getBlockAddr(blkSize);
        std::pair<Addr,tracker_entry_t> kv(blk_addr, {std::vector<IOHubQueueEntry*>({queue_entry_ptr}), pkt->isWrite()});
        cache_line_trackers.insert(kv);
        DPRINTF(IOHub, "%s: Create a new tracker vector for packet %s\n", __func__, pkt->print());
    } else {
        tracker_entry_t* tracker_entry = &(pos->second);
        tracker_entry->outstanding_reqs.push_back(queue_entry_ptr);
        DPRINTF(IOHub, "%s: Pushing packet %s to existing tracker vector\n", __func__, pkt->print());
    }
    queue_entry_ptr->setInService();
    ++used_trackers;
}

void IOHub::issueMemPacket(PacketPtr pkt, IOHubQueueEntry* queue_entry_ptr)
{
    PacketPtr out_packet = createMissPacket(pkt, pkt->needsWritable(), pkt->isWholeLineWrite(blkSize));
    out_packet->pushSenderState(queue_entry_ptr);
    Tick send_time = std::max<uint64_t>(queue_entry_ptr->get_forward_time(), curTick());
    schedule((Event*)(new IOHubSendMemReqEvent(out_packet, this)), send_time);
    DPRINTF(IOHub, "%s sending miss packet %s at time %lld\n", __func__, out_packet->print(), send_time);
}

bool IOHub::checkTrackerConflict(IOHubQueueEntry* queue_entry_ptr, bool* canIssue, Addr blk_addr)
{
    PacketPtr pkt = queue_entry_ptr->get_packet_ptr();
    assert(pkt != NULL);
    bool req_is_write = pkt->isWrite();

    std::unordered_map<Addr,tracker_entry_t>::iterator pos = cache_line_trackers.find(blk_addr);
    // Check trackers
    bool need_miss_packet = *canIssue;
    if (*canIssue && (pos != cache_line_trackers.end())) {
        // Tracker allocated to an outstanding request
        tracker_entry_t* tracker_entry = &(pos->second);
        if (req_is_write) {
            if (!(tracker_entry->hasOutstandingWriteReq)) {
                // There is an outstanding read request, we cannot issue the write request
                *canIssue = false;
                assert(pkt->isRequest());
                assert(!(queue_entry_ptr->getInService() || queue_entry_ptr->getIsComplete()));
                tracker_entry->outstanding_reqs.back()->add_waiting_req(queue_entry_ptr);
                queue_entry_ptr->setIsWaiting();
                DPRINTF(IOHub, "%s: Cannot issue write packet %s because of outstanding read to block %llx\n",
                        __func__, pkt->print(), blk_addr);
            }
        } else {
            // Read request
            if (tracker_entry->hasOutstandingWriteReq) {
                // There is an outstanding write request, we will issue the read request after write request completes
                *canIssue = false;
                assert(pkt->isRequest());
                assert(!(queue_entry_ptr->getInService() || queue_entry_ptr->getIsComplete()));
                tracker_entry->outstanding_reqs.back()->add_waiting_req(queue_entry_ptr);
                queue_entry_ptr->setIsWaiting();
                DPRINTF(IOHub, "%s: Cannot issue read packet %s because of outstanding write to block %llx\n",
                        __func__, pkt->print(), blk_addr);
            }
        }
        need_miss_packet = false;
    }

    return need_miss_packet;
}

void IOHub::recvTimingReq(PacketPtr pkt)
{
    assert(!lsqFull());

    promoteWholeLineWrites(pkt);

    Tick forward_time = clockEdge(forwardLatency) + pkt->headerDelay;
    // Consume the delays now
    pkt->headerDelay = 0;
    pkt->payloadDelay = 0;

    // Does the DMA device always split our requests into cache line size?
    assert(pkt->getSize() <= blkSize);
    Addr blk_addr = pkt->getBlockAddr(blkSize);

    IOHubQueueEntry* queue_entry_ptr = new IOHubQueueEntry(pkt, forward_time);
    ls_queue.push(queue_entry_ptr);
    ++num_used_queue_entries;
    bool canIssue = true;

    // We should only be receiving writes and reads from DMA device
    assert(pkt->isWrite() || pkt->isRead());
    bool req_is_write = pkt->isWrite();

    // Add write to store queue
    if (req_is_write) {
        squeue.push(queue_entry_ptr);
    } else {
        if (use_blocking_ordered_reads()) {
            lqueue.push(queue_entry_ptr);
        }
    }

    // Handle PCIe W->R ordering
    if (req_is_write) {
        latest_write = queue_entry_ptr;
    } else {
        // For read requests, check if they need to be ordered after a write
        if ((!use_speculative_reads()) && (latest_write != NULL)) {
            assert(pkt->isRequest());
            assert(!(queue_entry_ptr->getInService() || queue_entry_ptr->getIsComplete()));
            latest_write->add_waiting_req(queue_entry_ptr);
            canIssue = false;
            queue_entry_ptr->setIsWaiting();
            DPRINTF(IOHub, "%s: Cannot issue read packet %s because waiting for write packet %s\n",
                    __func__, pkt->print(), latest_write->get_packet_ptr()->print());
        }
    }

    if (canIssue && !req_is_write && use_blocking_ordered_reads()) {
        canIssue = (queue_entry_ptr == lqueue.front());
        if (!canIssue) {
            queue_entry_ptr->setIsWaiting();
            DPRINTF(IOHub, "%s: Cannot issue read packet %s because of blocking R->R order\n", __func__, pkt->print());
        }
    }

    // Check if tracker has already been allocated to block
    bool need_miss_packet = checkTrackerConflict(queue_entry_ptr, &canIssue, blk_addr);

    if (canIssue) {
        // Only consume trackers for requests that can be issued
        allocateTracker(pkt, queue_entry_ptr, blk_addr);
        if (need_miss_packet) {
            DPRINTF(IOHub, "Create and issue a new coherence request\n");
            issueMemPacket(pkt, queue_entry_ptr);
        }
    }

    assert(queue_entry_ptr->getInService() || queue_entry_ptr->getIsWaiting());
}

bool IOHub::recvTimingResp(PacketPtr pkt)
{
    assert(pkt->isResponse());

    // all header delay should be paid for by the crossbar, unless
    // this is a prefetch response from above
    panic_if(pkt->headerDelay != 0 && pkt->cmd != MemCmd::HardPFResp,
             "%s saw a non-zero packet delay\n", name());

    const bool is_error = pkt->isError();

    if (is_error) {
        DPRINTF(IOHub, "%s: IOHub received %s with error\n", __func__, pkt->print());
    }
    assert(!is_error);

    DPRINTF(IOHub, "%s: Handling response %s\n", __func__, pkt->print());


    IOHubQueueEntry* iohub_queue_entry = dynamic_cast<IOHubQueueEntry*>(pkt->popSenderState());
    assert(iohub_queue_entry);

    Tick completion_time = 0;
    PacketPtr req_packet = iohub_queue_entry->get_packet_ptr();
    assert(req_packet != NULL);
    assert(req_packet->isWrite() || req_packet->isRead());

    // For sending out requests that were waiting
    std::queue<IOHubQueueEntry*> out_queue;

    Addr blk_addr = req_packet->getBlockAddr(blkSize);
    // Get tracker entry
    std::unordered_map<Addr,tracker_entry_t>::iterator pos = cache_line_trackers.find(blk_addr);
    tracker_entry_t* tracker_entry = &(pos->second);
    int count = 0;
    while (!tracker_entry->outstanding_reqs.empty()) {
        IOHubQueueEntry* target = tracker_entry->outstanding_reqs.front();
        PacketPtr tgt_pkt = target->get_packet_ptr();
        assert(tgt_pkt != NULL);
        assert(tgt_pkt->isWrite() || tgt_pkt->isRead());
        DPRINTF(IOHub, "%s: Handling response for packet %s\n", __func__, tgt_pkt->print());
        if (tgt_pkt->isWrite()) {
            // If request was a write, reads cannot have been placed in tracker due to PCIe ordering.
            // Even if we allow speculative reads, we will not issue a read req for a block that we are waiting for a write
            assert(req_packet->isWrite());
            assert(pkt->isInvalidate());
            // Mark write as permitted
            std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator write_tracker = permitted_writes.find(blk_addr);
            if (write_tracker == permitted_writes.end()) {
                // Create entry
                std::pair<Addr,std::vector<IOHubQueueEntry*>> kv(blk_addr, std::vector<IOHubQueueEntry*>({target}));
                permitted_writes.insert(kv);
                DPRINTF(IOHub, "%s: IOHub creating write tracker for packet %s with block address %llx\n", __func__, tgt_pkt->print(), blk_addr);
            } else {
                std::vector<IOHubQueueEntry*>* write_list = &(write_tracker->second);
                write_list->push_back(target);
                DPRINTF(IOHub, "%s: IOHub adding packet %s to existing write tracker with block address %llx\n", __func__, tgt_pkt->print(), blk_addr);
            }
            target->set_response_time(curTick() + pkt->headerDelay);
            // We will be unblocking the waiting requests so do not let this write block any other requests
            if (isLatestWrite(target)) {
                latest_write = NULL;
            }
        } else {
            // If request was a read, writes cannot have been placed in tracker while waiting for coherence req to complete.
            assert(req_packet->isRead());
            assert(pkt->isRead());
            assert(pkt->hasData());
            assert(pkt->matchAddr(tgt_pkt));
            assert(pkt->getSize() >= tgt_pkt->getSize());

            target->set_resp_packet(pkt);
            
            if ((use_baseline_ioh() && !resp_inorder) || use_blocking_ordered_reads()) {
                // Responses do not need to be sent in-order
                // Note that blocking ordered reads gives us ordered responses anyway
                // We respond in order when using ordered reads
                // 1 cycle between sending each response
                completion_time = pkt->headerDelay + clockEdge(responseLatency + Cycles(count)) + pkt->payloadDelay;
                tgt_pkt = target->applyReadResponse();
                // Reset the bus additional time as it is now accounted for
                tgt_pkt->headerDelay = tgt_pkt->payloadDelay = 0;
                DPRINTF(IOHub, "%s: IOHub scheduling response packet %s at %lld\n", __func__, tgt_pkt->print(), completion_time);
                schedule((Event*) (new IOHubSendReadRespEvent(tgt_pkt, this)), completion_time);
                ++count;
            } else {
                // (use_baseline_ioh() && resp_inorder) || use_speculative_reads()
                // Order read responses (as required by D5b in Table 2-39 in the PCIe spec.)
                completion_time = pkt->headerDelay + clockEdge(responseLatency) + pkt->payloadDelay;
                target->set_response_time(completion_time);
                DPRINTF(IOHub, "%s: IOHub deferring response packet %s to %lld\n", __func__, tgt_pkt->print(), completion_time);
            }

            if (use_speculative_reads()) {
                std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator spec_tracker = speculating_reads.find(blk_addr);
                if (spec_tracker == speculating_reads.end()) {
                    // Create entry
                    std::pair<Addr,std::vector<IOHubQueueEntry*>> kv(blk_addr, std::vector<IOHubQueueEntry*>({target}));
                    speculating_reads.insert(kv);
                    DPRINTF(IOHub, "%s: IOHub creating spec tracker for packet %s with block address %llx\n", __func__, tgt_pkt->print(), blk_addr);
                } else {
                    std::vector<IOHubQueueEntry*>* spec_list = &(spec_tracker->second);
                    spec_list->push_back(target);
                    DPRINTF(IOHub, "%s: IOHub adding packet %s to existing spec tracker with block address %llx\n", __func__, tgt_pkt->print(), blk_addr);
                }
            }
        }

        // Update LSQ entry
        target->clearInService();
        target->setIsComplete();
        assert(!target->getIsCommitted());

        // Clean up tracker
        tracker_entry->outstanding_reqs.erase(tracker_entry->outstanding_reqs.begin());
        --used_trackers;

        // If target is read: Send out write requests that were waiting for the read coherence request to complete.
        // If target is write: Send out read requests that are ordered after the write.
        target->send_waiting_reqs(&out_queue);

        // Unblock device side port if resources are available to accept new requests
        if (devSidePort.isBlocked() && !lsqFull() && !noTrackers()) {
            devSidePort.clearBlocked();
        }
    }

    // Delete tracker entry
    cache_line_trackers.erase(pos);

    assert(out_queue.size() <= lsq_size);
    // Send out waiting entries 
    while (!out_queue.empty()) {
        IOHubQueueEntry* entry_ptr = out_queue.front();
        assert(entry_ptr->getIsWaiting());
        entry_ptr->clearIsWaiting();
        PacketPtr next_pkt = entry_ptr->get_packet_ptr();
        assert(next_pkt != NULL);
        assert(next_pkt->isRequest());
        Addr blk_addr = next_pkt->getBlockAddr(blkSize);
        // We do not worry about PCIe ordering here since LSQ records the ordering of request
        bool canIssue = true;
        if (next_pkt->isRead() && use_blocking_ordered_reads()) {
            // We will send these reads in cleanLSQ
            canIssue = false;
            entry_ptr->setIsWaiting();
        }
        bool need_miss_packet = checkTrackerConflict(entry_ptr, &canIssue, blk_addr);
        if (canIssue) {
            allocateTracker(next_pkt, entry_ptr, blk_addr);
            if (need_miss_packet) {
                DPRINTF(IOHub, "%s: Create and issue a new coherence request for packet %s\n", __func__, next_pkt->print());
                issueMemPacket(next_pkt, entry_ptr);
            }
        }
        out_queue.pop();
    }

    cleanLSQ();
    
    return true;
}

void IOHub::memSidePortSendReq(PacketPtr pkt)
{
    memSidePort.sendRequest(pkt, false);
}

void IOHub::devSidePortSendResp(PacketPtr pkt)
{
    devSidePort.sendResponse(pkt, false);
}

bool IOHub::lsqFull()
{
    return (num_used_queue_entries == lsq_size);
}

bool IOHub::noTrackers()
{
    return (used_trackers == num_trackers);
}

bool IOHub::isLatestWrite(IOHubQueueEntry* write_entry_ptr) 
{
    return (write_entry_ptr == latest_write);
}

void IOHub::cleanLSQ() {
    // Track number of responses sent when responding in-order
    Tick send_time = curTick();
    
    if ((!ls_queue.empty()) && (ls_queue.front()->getIsComplete())) {
        // This is the earliest time we can schedule a response
        send_time = std::max<uint64_t>(ls_queue.front()->get_response_time(), send_time);
        send_time = std::max<uint64_t>(last_response_time + cyclesToTicks(Cycles(1)), send_time);
    }

    while ((!squeue.empty()) && (squeue.front()->getIsComplete())) {
        // PCIe writes are allowed to pass reads
        // Commit writes at the head of the store queue
        IOHubQueueEntry* entry_ptr = squeue.front();
        PacketPtr dma_wr_pkt = entry_ptr->get_packet_ptr();
        assert(dma_wr_pkt != NULL);
        assert(dma_wr_pkt->isWrite());
        Addr blk_addr = dma_wr_pkt->getBlockAddr(blkSize);
        // Fast send because in reality, the IOHub writes to a local write cache that can be snooped
        send_time += cyclesToTicks(Cycles(1));
        RequestPtr wb_req = std::make_shared<Request>(blk_addr, blkSize, 0, Request::wbRequestorId);
        wb_req->taskId(dma_wr_pkt->req->taskId());
        PacketPtr wb_pkt = new Packet(wb_req, MemCmd::WritebackDirty);
        wb_pkt->allocate();
        wb_pkt->setData(dma_wr_pkt->getConstPtr<uint8_t>());
        // Reset the bus additional time as it is now accounted for
        wb_pkt->headerDelay = wb_pkt->payloadDelay = 0;
        DPRINTF(IOHub, "%s: IOHub sending write packet %s on system bus at head of lsq at time %lld\n", __func__, wb_pkt->print(), send_time);
        schedule((Event*)(new IOHubSendMemReqEvent(wb_pkt, this)), send_time);
        entry_ptr->setIsCommitted();

        // Clean up the write tracker here
        std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator write_tracker = permitted_writes.find(blk_addr);
        assert(write_tracker != permitted_writes.end());
        std::vector<IOHubQueueEntry*>* perm_writes = &(write_tracker->second);
        bool found_write = false;
        for (size_t i = 0; i < perm_writes->size(); ++i) {
            if ((*perm_writes)[i] == entry_ptr) {
                // Found matching entry
                perm_writes->erase(perm_writes->begin() + i);
                found_write = true;
                DPRINTF(IOHub, "%s: IOHub deleting write tracker for packet %s\n", __func__, dma_wr_pkt->print());
                break;
            }
        }
        assert(found_write);
        if (perm_writes->empty()) {
            permitted_writes.erase(write_tracker);
        }

        // Posted write request "response"
        dma_wr_pkt->makeTimingResponse();
        dma_wr_pkt->headerDelay = dma_wr_pkt->payloadDelay = 0;
        assert(dma_wr_pkt->isWrite());
        schedule((Event*) (new IOHubSendReadRespEvent(dma_wr_pkt, this)), curTick());

        squeue.pop();
        --num_used_queue_entries;
    }

    if (use_blocking_ordered_reads() && !lqueue.empty() && lqueue.front()->getIsComplete()) {
        // Mark read as committed
        lqueue.front()->setIsCommitted();
        lqueue.pop();
        --num_used_queue_entries;
    }

    while ((!ls_queue.empty()) && (ls_queue.front()->getIsComplete())) {
        IOHubQueueEntry* entry_ptr = ls_queue.front();
        PacketPtr req_pkt = entry_ptr->get_packet_ptr();

        // Clean committed writes (and reads in the case of blocking ordered reads) out of the LSQ
        if (entry_ptr->getIsCommitted()) {
            // No need to update num_used_queue_entries here since they are already accounted for when
            // the store queue entry is marked as committed.
            assert(req_pkt == NULL);
            // Note: we cannot deref the packet ptr here
            DPRINTF(IOHub, "%s: Removing committed packet from LSQ, use_blocking_ordered_reads:%d\n", __func__, use_blocking_ordered_reads());
            // Committed writes should not exist at the head of the store queue since we clean
            // the store queue when responding to read requests.
            assert((squeue.empty()) || (entry_ptr != squeue.front()));
            // Same for reads
            assert((lqueue.empty()) || (entry_ptr != lqueue.front()));
            ls_queue.pop();
            delete entry_ptr;
            continue;
        }

        assert(req_pkt != NULL);
        // We must have taken care of all the writes that can be committed
        assert(!req_pkt->isWrite());

        if (((use_baseline_ioh() && resp_inorder) || use_speculative_reads()) && (req_pkt->isRead())) {
            // We need to respond in order and the read request is completed and at the head of the lsq
            send_time += cyclesToTicks(Cycles(1));
            PacketPtr tgt_pkt = entry_ptr->applyReadResponse();
            // Reset the bus additional time as it is now accounted for
            tgt_pkt->headerDelay = tgt_pkt->payloadDelay = 0;
            DPRINTF(IOHub, "%s: IOHub sending response packet %s for request at head of lsq at time %lld\n", __func__, tgt_pkt->print(), send_time);
            schedule((Event*) (new IOHubSendReadRespEvent(tgt_pkt, this)), send_time);
            last_response_time = send_time;

            if (use_speculative_reads()) {
                // Clean up the speculative read invalidation tracker here
                Addr blk_addr = tgt_pkt->getBlockAddr(blkSize);
                std::unordered_map<Addr,std::vector<IOHubQueueEntry*>>::iterator spec_tracker = speculating_reads.find(blk_addr);
                assert(spec_tracker != speculating_reads.end());
                std::vector<IOHubQueueEntry*>* spec_reads = &(spec_tracker->second);
                bool found_spec_read = false;
                for (size_t i = 0; i < spec_reads->size(); ++i) {
                    if ((*spec_reads)[i] == entry_ptr) {
                        // Found matching entry
                        spec_reads->erase(spec_reads->begin() + i);
                        found_spec_read = true;
                        DPRINTF(IOHub, "%s: IOHub deleting spec tracker for packet %s\n", __func__, tgt_pkt->print());
                        break;
                    }
                }
                assert(found_spec_read);
                if (spec_reads->empty()) {
                    speculating_reads.erase(spec_tracker);
                }
            }
        }

        DPRINTF(IOHub, "%s: Removing read packet %s from LSQ\n", __func__, entry_ptr->get_packet_ptr()->print());
        ls_queue.pop();
        delete entry_ptr;
        --num_used_queue_entries;
    }

    // At this point, the head of the lsq must be an incomplete request
    assert(ls_queue.empty() || !(ls_queue.front()->getIsComplete()));
    if (!(ls_queue.empty()) && !(ls_queue.front()->getInService())) {
        // If the head is not in service, it must have been invalidated so we re-issue
        IOHubQueueEntry* queue_entry_ptr = ls_queue.front();
        PacketPtr resend_pkt = queue_entry_ptr->get_packet_ptr();
        Addr blk_addr = resend_pkt->getBlockAddr(blkSize);
        bool canIssue = true;
        if (use_blocking_ordered_reads() && resend_pkt->isRead()) {
            // Here we send out the next read request, enforcing R->R order.
            assert(queue_entry_ptr == lqueue.front());
            assert(queue_entry_ptr->getIsWaiting());
            queue_entry_ptr->clearIsWaiting();
        } else {
            // In all other cases, we only end up here if an issued request was invalidated
            assert(queue_entry_ptr->getInvalidated());
            queue_entry_ptr->clearInvalidated();
        }
        // We still need to check whether this request can be satisfied by an outstanding coherence request
        bool need_miss_packet = checkTrackerConflict(queue_entry_ptr, &canIssue, blk_addr);
        DPRINTF(IOHub, "%s: IOHub retrying packet %s at time %lld. canIssue:%d, needMissPacket:%d\n",
                __func__, resend_pkt->print(), send_time, canIssue, need_miss_packet);
        if (canIssue) {
            allocateTracker(resend_pkt, queue_entry_ptr, blk_addr);
            if (need_miss_packet) {
                DPRINTF(IOHub, "%s: Create and issue a new coherence request\n", __func__);
                issueMemPacket(resend_pkt, queue_entry_ptr);
            }
        }
    }
}

Port& IOHub::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dev_side_port") {
        return devSidePort;
    } else if (if_name == "mem_side_port") {
        return memSidePort;
    }  else {
        return ClockedObject::getPort(if_name, idx);
    }
}

IOHub::DevSidePort::DevSidePort(const std::string& _name, IOHub& _iohub) : 
ResponsePort(_name, (PortID) 0), sendRetryEvent([this]{ processSendRetry(); }, _name), 
iohub{_iohub}, blocked(false), response_blocked(false), mustSendRetry(false)
{}

void IOHub::DevSidePort::processSendRetry()
{
    DPRINTF(IOHub, "DevSidePort is sending retry\n");

    // reset the flag and call retry
    mustSendRetry = false;
    sendRetryReq();
}

bool IOHub::DevSidePort::tryTiming(PacketPtr pkt)
{
    
    if (iohub.lsqFull() || iohub.noTrackers()) {
        mustSendRetry = true;
        setBlocked();
        return false;
    } else if (blocked || mustSendRetry) {
        mustSendRetry = true;
        return false;
    } else {
        mustSendRetry = false;
        return true;
    }
}

bool IOHub::DevSidePort::recvTimingReq(PacketPtr pkt)
{
    assert(pkt->isRequest());

    if (tryTiming(pkt)) {
        iohub.recvTimingReq(pkt);
        return true;
    }

    return false;
}

void IOHub::DevSidePort::recvRespRetry()
{
    response_blocked = false;
    while ((retry_resp_queue.size() > 0) && !response_blocked) {
        PacketPtr pkt = retry_resp_queue.front();
        sendResponse(pkt, true);
    }
}

void IOHub::DevSidePort::setBlocked()
{
    assert(!blocked);

    blocked = true;
}

void IOHub::DevSidePort::clearBlocked()
{
    assert(blocked);

    blocked = false;

    if (mustSendRetry) {
        iohub.schedule(sendRetryEvent, curTick());
    }
}

void IOHub::DevSidePort::sendResponse(PacketPtr pkt, bool isRetry)
{
    if (isRetry) {
        if (sendTimingResp(pkt)) {
            retry_resp_queue.erase(retry_resp_queue.begin());
            DPRINTF(IOHub, "%s: IOHub retry for response packet %s successful\n", __func__, pkt->print());
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
            DPRINTF(IOHub, "%s: IOHub sending response packet %s successful\n", __func__, pkt->print());
        }
    }
}

IOHub::MemSidePort::MemSidePort(const std::string &_name, IOHub& _iohub) : RequestPort(_name, (PortID) 0), iohub(_iohub)
{
    blocked = false;
}

void IOHub::MemSidePort::sendRequest(PacketPtr pkt, bool isRetry)
{
    if (isBlocked()) {
        DPRINTF(IOHub, "%s fail, port already blocked, blocking packet:%s\n", __func__, pkt->print());
        pending_req_queue.push(pkt);
    } else if (!sendTimingReq(pkt)) {
        DPRINTF(IOHub, "%s fail, blocking port, blocking packet:%s\n", __func__, pkt->print());
        if (!isRetry) {
            // New packet so push to pending queue
            pending_req_queue.push(pkt);
        }
        setBlocked();
    } else {
        DPRINTF(IOHub, "%s success, packet:%s\n", __func__, pkt->print());
    }
}

void IOHub::MemSidePort::recvReqRetry()
{
    assert(!pending_req_queue.empty());

    clearBlocked();
}

void IOHub::MemSidePort::clearBlocked()
{
    assert(blocked);

    blocked = false;

    while (!pending_req_queue.empty() && !blocked) {
        PacketPtr pkt = pending_req_queue.front();
        sendRequest(pkt, true);
        if (!blocked) {
            // If port is blocked, don't remove packet
            pending_req_queue.pop();
        }
    }
}

void IOHub::MemSidePort::recvTimingSnoopReq(PacketPtr pkt)
{
    // handle snooping requests
    iohub.recvTimingSnoopReq(pkt);
}

} // NAMESPACE GEM5
