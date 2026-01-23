#ifndef __DEV_MYDMA_DEVICE_REG_MAP_HH__
#define __DEV_MYDMA_DEVICE_REG_MAP_HH__

#include "my_dma.hh"

namespace my_dma_reg_addr_map
{
    const gem5::Addr MY_DMA_MAGIC_NUMBER = 0x00;
    const gem5::Addr MY_DMA_TARGET_ADDRS = 0x08;
    const gem5::Addr MY_DMA_TRANSFR_SIZE = 0x10;
    const gem5::Addr MY_DMA_TRANSFER_DIR = 0x18;
    const gem5::Addr MY_DMA_LAST_ADDRESS = 0x18;
}

namespace spsc_dma_reg_addr_map
{
    const gem5::Addr SPSC_DMA_CTRL_REG   = 0x00;
    const gem5::Addr SPSC_DMA_QUEUE_SIZE = 0x08;
    const gem5::Addr SPSC_DMA_QUEUE_BASE = 0x10;
    const gem5::Addr SPSC_DMA_QUEUE_HEAD = 0x18;
    const gem5::Addr SPSC_DMA_QUEUE_TAIL = 0x20;
    const gem5::Addr SPSC_DMA_LAST_ADDR  = 0x20;
}

namespace cell_dma_reg_addr_map
{
    const gem5::Addr CELL_DMA_CTRL_REG  = 0x00;
    const gem5::Addr CELL_DMA_MEM_ADDR  = 0x08;
    const gem5::Addr CELL_DMA_CELL_SIZE = 0x10;
    const gem5::Addr CELL_P2P_DEV_ADDR  = 0X18;
    const gem5::Addr CELL_DMA_LAST_ADDR = 0x18;
}

namespace p2pw_dma_reg_addr_map
{
    const gem5::Addr P2PW_DMA_CTRL_REG  = 0x00;
    const gem5::Addr P2PW_DMA_MEM_ADDR  = 0x08;
    const gem5::Addr P2PW_DMA_MEM_SIZE = 0x10;
    const gem5::Addr P2PW_DMA_LAST_ADDR = 0x10;
}

#endif // __DEV_MYDMA_DEVICE_REG_MAP_HH__

