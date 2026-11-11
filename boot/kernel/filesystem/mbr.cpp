#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "filesystem/mbr.hpp"


MBR::MBR() {
    BootDrive = nullptr;
    SizeInBytes = 0;
    memcpy(PartitionName, "MBR", sizeof("MBR")); 
}


u32 MBR::DiscoverFileSystem() {
    if (BootDrive == nullptr)
        return 0;

    u8 BootSector[512] = {0};

    BootDrive->read_sectors(0, 1, BootSector); // read sector 0 into the BootSector buffer

    MBRPartition *Partition = (MBRPartition *)(BootSector + 0x1BE);

    u8 type = Partition->PartitionType;
    if (type == 0x0B || type == 0x0C)
        memcpy(fs, "FAT32", sizeof("FAT32"));

    u32 TotalSectors = Partition->TotalSectors;
    // MBR partition lengths are in 512-byte sectors, including on 4 KiB drives.
    SizeInBytes = (u64)TotalSectors * 512;
    return Partition->StartingLBA;
}
