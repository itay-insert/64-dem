#pragma once

#include "uint_definitions.h"

class disk_drive {
    public:
        char vendor[5];
        u8 disk_type;
        u8 disk_index;
        virtual void read_sectors(u32 lba, u32 sector_count, void *buffer) = 0;
        virtual void write_sectors(u32 lba, u32 sector_count, void *buffer) = 0;
        virtual void init() = 0;
};


class storage {
    private:
        void increase_drives(u64 ClassSize);

    public:
        disk_drive *drives;
        u8 drive_count;
        u64 drives_size;
        storage();
};