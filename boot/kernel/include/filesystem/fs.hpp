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
    u32 starting_cluster;
    u32 last_cluster;
    u8 *cache;
    int cluster_count;
    u64 directory_offset;
    u32 file_size;
    bool dirty;
    u32 references;
} file_cache;

typedef struct {
    u32 index;
    char *str;
    file_cache *open_files;
    int file_count;
    int cluster_count;
    u32 starting_cluster;
} Paths_table;

typedef struct {
    bool status;
    u32 index;
} Path_ret;

class BootFileSystem {
    private:
        u32 first_data_lba = 0;
        u8 SectorsPerCluster = 0;
        u8 fat_copies = 0;
        u32 sectors_per_fat = 0;
        u32 cluster_size = 0;
        u32 fat_lba = 0;
        u32 RootCluster = 0;
        u32 Cluster_To_LBA(u32 cluster);
        u32 find_next_cluster(u32 cluster);
        u32 load_clusters(u32 cluster, u32 count, void *buffer);
        u32 write_clusters(u32 cluster, u32 count, void *buffer);
        u64 *Paths = nullptr;
        Paths_table *table = nullptr;
        u32 Path_count = 0;
        u32 *cluster_map = nullptr;
        u32 clusters = 0;
        bool scan_fat();
        Path_ret increase_paths(u64 address, char *path, int cluster_count, u32 starting_cluster);
        bool valid_cluster(u32 cluster) const;
        FAT32_DIR *find_entry(u32 index, const char *filename) const;
        int reserve_file_slot(Paths_table &parent);
        file_cache *get_open_file(u64 handle) const;
        bool ensure_file_clusters(file_cache &file, u32 count, bool extend);
        void set_fat_entry(u32 cluster, u32 value);
        u32 find_free_cluster(u32 start = 2);

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
        Path_ret find_open_path(const char *path) const;
        bool lookup_entry(u32 index, const char *filename, FAT32_DIR &entry) const;
        u64 open_file_by_path(const char *path, const char *filename);
        u64 open_file_by_index(u32 index, const char *filename);
        void close_file(u64 file_handle);
        void close_path(u32 path_index);
        bool read_file(u64 file_handle, u64 offset, u32 size, void *buffer);
        bool write_file(u64 file_handle, u64 offset, u32 size, const void *buffer);
        bool fat32_make_entry(u32 path_index, const char *entry_name, bool read_only = false);
        bool truncate_file(u64 handle);
        void list_path(u32 index);
        // Look up an open file. A cache miss returns 0xFFFFFFFF.
        u64 get_file_handle(const char *path, const char *filename);
        // Copy the cached directory entry. An out-of-range handle returns a zeroed entry.
        FAT32_DIR get_file_dir(u64 file_handle) const;
        char *return_str(u32 path_index);
        u64 get_filesize(u64 file_handle);
};


char *convert_str_to_fat32(const char *str);
char *convert_fat32_to_str(const char *fat32);
