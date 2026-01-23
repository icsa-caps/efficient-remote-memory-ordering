#ifndef __DEV_MMIO_DEVICE_HH__
#define __DEV_MMIO_DEVICE_HH__

#include <cstdlib>

#include <queue>
#include <map>

#include "dev/io_device.hh"

#include "params/MmioDevice.hh"

#include "base/statistics.hh"

namespace gem5
{

class MmioDevice : public BasicPioDevice{
private:
    Addr next_expected_addr;
    Addr mmio_end_addr;
    uint8_t* device_memory;

protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

public:
    PARAMS(MmioDevice);
    MmioDevice(const Params &p);
    ~MmioDevice();
};

}

#endif