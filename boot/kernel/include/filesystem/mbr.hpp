#pragma once

#include "filesystem/partition.hpp"

typedef struct {
    u8 BootIndicator; // offset 0x1BE
    u8 Starting_CHS_low; // offset 0x1BF
    u8 Starting_CHS_mid; // offset 0x1C0
    u8 Starting_CHS_High; // offset 0x1C1
    u8 PartitionType; // offset 0x1C2
    u8 Ending_CHS_low; // offset 0x1C3
    u8 Ending_CHS_mid; // offset 0x1C4
    u8 Ending_CHS_High; // offset 0x1C5
    u32 StartingLBA; // offset 0x1C6
    u32 TotalSectors; // offset 0x1CA
} __attribute__((packed)) MBRPartition;


class MBR : public BootPartition {
    public:
        u32 DiscoverFileSystem() override;
        MBR();
};