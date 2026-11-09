#pragma once

#include "drivers/storage/storage.hpp"
#include "drivers/storage/ide/ata.h"

typedef struct {
    ata_drive_t disk;
    u32 block_count;
    bool set;
    bool found;
} ata_drive;


class ATA_drive : public disk_drive {
    private:
        ata_drive drive;

    public:
        void read_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void write_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void init() override;
};