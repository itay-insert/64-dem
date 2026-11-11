#pragma once

#include "filesystem/mbr.hpp"

typedef struct {
    char filename[11];
    u8 attributes;
    u8 ReservedNT;
    u8 CreationTime;
    u16 Time;
    u16 Date;
    u16 last_access;
    u16 cluster_high;
    u16 last_modify_time;
    u16 last_modify_date;
    u16 cluster_low;
    u32 file_size;
} __attribute__((packed)) FAT32_DIR;


typedef struct {
    u8 Sequence_number;
    u16 first_part[5];
    u8 attribute; // always equals 0x0F
    u8 long_entry_type; // 0 for name entries
    u8 checksum;
    u16 second_part[6];
    u16 Padding; // always 0
    u16 final_part[2];
} __attribute__((packed)) FAT32_LFN;

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
        int scan_dir(FAT32_DIR *dir, u32 sector_count);

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