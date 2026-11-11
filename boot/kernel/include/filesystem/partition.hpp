#pragma once

#include "drivers/storage/storage.hpp"

class BootPartition {
    public:
        char PartitionName[4];
        char fs[6]; // filesystem_name
        disk_drive *BootDrive;
        u64 SizeInBytes;
        void SetBootDrive(disk_drive *drive);
        virtual u32 DiscoverFileSystem() = 0;
};