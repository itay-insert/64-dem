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
    u8 *cache;
    int cluster_index;
} cache_descriptor;

typedef struct {
    u32 starting_cluster;
    cache_descriptor *cache;
    int cluster_count;
    char *name;
} file_cache;


typedef struct {
    u32 index;
    char *str;
    file_cache *open_files;
    int file_count;
} Paths_table;

typedef struct {
    u8 *cache;
    u32 index;
} Search_ret;

typedef struct {
    bool status;
    u32 index;
} Path_ret;


class BootFileSystem {
    private:
        u32 first_data_lba = 0;
        u8 SectorsPerCluster = 0;
        u8 NumOfFATS = 0;
        u16 RootEntries = 0;
        u32 cluster_size = 0;
        u32 fat_lba = 0;
        u32 RootCluster = 0;
        u32 Cluster_To_LBA(u32 cluster);
        u32 LBA_to_Cluster(u32 lba);
        u32 find_next_cluster(u32 cluster);
        u32 load_clusters(u32 cluster, u32 count, void *buffer);
        u64 *Paths;
        Paths_table *table;
        u32 Path_count;
        Path_ret increase_paths(u64 address, char *path);
        bool increase_files(Paths_table *table, char *filename, u32 cluster);
        int scan_dir(u32 cluster);
        Search_ret search_path(char *path);
        void allocate_clusters(file_cache *file, u32 cluster_count);
        void fat32_link(u32 current, u32 next);
        void fat32_unlink(u32 cluster);
        void fat32_make_entry(u32 path_index, const char *entry_name);

    public:
        u64 SizeInBytes;
        u64 SizeUsed;
        u64 SizeFree;
        u32 Partition_lba;
        u32 root_lba;
        disk_drive *drive;
        BootPartition *Partition;
        void fs_init(disk_drive *boot_drive, BootPartition *partition, u32 lba, u64 size);
        Path_ret open_path(const char *path);
        u64 open_file_by_path(const char *path, const char *filename);
        u64 open_file_by_index(u32 index, const char *filename);
        void close_file(u64 file_handle);
        void close_path(u32 path_index);
        void read_file(u64 file_handle, u64 offset, u32 size, void *buffer);
};