#pragma once

#include "filesystem/mbr.hpp"

typedef struct {
    u32 index;
    char *str;
} Paths_table;

class BootFileSystem {
    private:
        u32 first_data_lba = 0;
        u8 SectorsPerCluster = 0;
        u8 NumOfFATS = 0;
        u16 RootEntries = 0;
        u32 cluster_size = 0;
        u32 Cluster_To_LBA(u32 cluster);
        u32 LBA_to_Cluster(u32 lba);
        u64 *Paths;
        Paths_table *table;
        u32 Path_count;
        bool increase_paths(u64 address, char *path);

    public:
        u64 SizeInBytes;
        u64 SizeUsed;
        u64 SizeFree;
        u32 Partition_lba;
        u32 root_lba;
        disk_drive *drive;
        BootPartition *Partition;
        void fs_init(disk_drive *boot_drive, BootPartition *partition, u32 lba, u64 size);
        bool open_path(const char *path);
};