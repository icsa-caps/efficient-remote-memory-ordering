#include "mmio_device.hh"

#include "mem/packet.hh"
#include "mem/packet_access.hh"

#include "base/logging.hh"
#include "base/trace.hh"

#include "debug/MmioDevice.hh"

namespace gem5
{

MmioDevice::MmioDevice(const Params &p) : BasicPioDevice(p, p.device_memory_size)
{
    next_expected_addr = pioAddr;
    mmio_end_addr = pioAddr + p.device_write_limit;
    device_memory = new uint8_t[pioSize];
}

MmioDevice::~MmioDevice()
{
    delete [] device_memory;
}

Tick MmioDevice::write(PacketPtr pkt)
{
    Addr current_wr_addr = pkt->getAddr();
    DPRINTF(MmioDevice, "%s: Write address = %llx, Expected address = %llx\n", __func__, current_wr_addr, next_expected_addr);
    assert(current_wr_addr == next_expected_addr);
    Addr index = current_wr_addr - pioAddr;
    memcpy(&(device_memory[index]), pkt->getConstPtr<uint8_t>(), pkt->getSize());
    next_expected_addr = current_wr_addr + 64;
    if (next_expected_addr >= mmio_end_addr) {
        // Wrap back to the start
        next_expected_addr = pioAddr;
    }
    pkt->makeTimingResponse();
    return pioDelay;
}

Tick MmioDevice::read(PacketPtr pkt)
{
    Addr index = pkt->getAddr() - pioAddr;
    pkt->setData(&(device_memory[index]));
    pkt->makeTimingResponse();
    return pioDelay;
}

} // namespace gem5
