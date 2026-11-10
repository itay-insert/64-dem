#pragma once

#include "drivers/storage/storage.hpp"
#include "x86-64/memory/memory.h"

typedef struct {
    u8 port_number;
    u8 device_type;
    bool connected;
    bool device_present;
    bool initialized;
    u32 sector_size;
    u64 capacity;
    char serial[21];
    char firmware[9];
    char model[41]; 
    u8 packet_size;
    dma_ret Command_list;
    dma_ret Command_Table;
    dma_ret identify;
    dma_ret FIS;
    dma_ret data;
} port_descriptor;


class SATA_drive : public disk_drive {
    private:
        port_descriptor *drive_port;

    public:
        void read_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void write_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void init() override;
};