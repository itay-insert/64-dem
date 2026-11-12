#include "filesystem/fs.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/kmalloc.h"

#define LFN 0x0F

typedef struct {
    u8 Padding0; // the first 3 bytes disassemble to JMP short 3C NOP
    u16 Padding1;
    u64 OEM_Identifier;  // OEM identifier of the dos version being used
    u16 BytesPerSector; // bytes per sector
    u8 SectorsPerCluster;
    u16 ReservedSectors;
    u8 NumOfFATS; // number of FATs usually 2
    u16 RootEntries;
    u16 TotalSectors; // if TotalSectors is 0, there are more than 0xFFFF sectors
    u8 MediaDescriptorType;
    u16 SectorsPerFAT;
    u16 SectorsPerTrack;
    u16 Heads;
    u32 HiddenSectors; // at the beginning of the partition
    u32 LargeSectorCount; // that field is set only if there are more than 0xFFFF sectors
} __attribute__((packed)) FAT32_BR;


typedef struct {
    u32 SectorsPerFAT;
    u16 Flags;
    u16 FATversion; // high byte is the major version and low byte is the minor version
    u32 RootCluster;
    u16 FSInfoSector;
    u16 backupBootSector;
    u64 Reserved0;
    u32 Reserved1;
    u8 DriveNumber;
    u8 NTFlags;
    u8 Signature;
    u32 VolumeIDSerial;
    char VolumeLabelString[11];
    char IdentifierString[8]; // always "FAT32  ", never to trust it
    u8 BootCode[420];
    u16 BootablePartitionSignature; // 0xAA55
} __attribute__((packed)) FAT32_EBR;


typedef struct {
    u32 LeadSignature; // must be 0x41615252
    u8 Reserved0[480];
    u32 AnotherSignature; // must be 0x61417272
    u32 LastFreeCluster;
    u32 LastAllocatedCluster;
    u8 Reserved1[12];
    u32 TrialSignature; // 0xAA550000
} __attribute__((packed)) FAT32_FSinfo;

static_assert(sizeof(FAT32_BR) + sizeof(FAT32_EBR) == 512);
static_assert(sizeof(FAT32_FSinfo) == 512);



u32 BootFileSystem::Cluster_To_LBA(u32 cluster) {
    u32 lba = first_data_lba + ((cluster - 2) * SectorsPerCluster); // cluster size would practically almost always be powers of two
    return lba;
}


u32 BootFileSystem::LBA_to_Cluster(u32 lba) {
    u32 cluster = 2 + (lba - first_data_lba) / SectorsPerCluster;
    return cluster; 
}

u8 *dir_place = nullptr;


void BootFileSystem::fs_init(disk_drive *boot_drive, BootPartition *partition, u32 lba, u64 size) {
    if (boot_drive == nullptr || lba == 0 || size == 0) {
        printf("BOOT_DRIVE : FAT32: no boot partition found\n");
        return;
    }

    Paths = nullptr;
    Path_count = 0;

    SizeInBytes = size;
    Partition_lba = lba;
    Partition = partition;
    drive = boot_drive;

    u8 *sector = new u8[512];
    memset(sector, 0, 512);
    boot_drive->read_sectors(lba, 1, sector);

    FAT32_BR *br = (FAT32_BR *)sector;
    FAT32_EBR *ebr = (FAT32_EBR *)(sector + sizeof(FAT32_BR));

    u16 fs_info_sector = ebr->FSInfoSector;
    u16 backup_boot_sector = ebr->backupBootSector;
    u32 root_cluster = ebr->RootCluster;
    u16 reserved_sectors = br->ReservedSectors;
    if (br->BytesPerSector != 512 || fs_info_sector == 0 ||
        fs_info_sector >= reserved_sectors ||
        ebr->BootablePartitionSignature != 0xAA55) {
        printf("BOOT_DRIVE : FAT32: invalid boot sector at LBA %u\n", lba);
        delete[] sector;
        return;
    }

    first_data_lba = lba + reserved_sectors + ((u32)br->NumOfFATS * ebr->SectorsPerFAT);
    NumOfFATS = br->NumOfFATS;
    SectorsPerCluster = br->SectorsPerCluster;
    RootEntries = br->RootEntries;

    u32 fs_info_lba = lba + fs_info_sector;
    memset(sector, 0, 512);
    boot_drive->read_sectors(fs_info_lba, 1, sector);
    FAT32_FSinfo *fs_info = (FAT32_FSinfo *)sector;
    bool valid_fs_info = fs_info->LeadSignature == 0x41615252 &&
        fs_info->AnotherSignature == 0x61417272 &&
        fs_info->TrialSignature == 0xAA550000;

    if (!valid_fs_info && backup_boot_sector != 0 &&
        (u32)backup_boot_sector + fs_info_sector < reserved_sectors) {
        u32 backup_lba = lba + backup_boot_sector + fs_info_sector;
        memset(sector, 0, 512);
        boot_drive->read_sectors(backup_lba, 1, sector);
        valid_fs_info = fs_info->LeadSignature == 0x41615252 &&
            fs_info->AnotherSignature == 0x61417272 &&
            fs_info->TrialSignature == 0xAA550000;
        if (valid_fs_info)
            printf("BOOT_DRIVE : FAT32: using backup FSInfo at LBA %u\n", backup_lba);
        fs_info_lba = backup_lba;
    }

    if (!valid_fs_info) {
        printf("BOOT_DRIVE : FAT32: invalid fs_info at LBA %u (%x %x %x)\n",
               fs_info_lba, fs_info->LeadSignature,
               fs_info->AnotherSignature, fs_info->TrialSignature);
        delete[] sector;
        return;
    }

    cluster_size = SectorsPerCluster * 512;

    u32 free_clusters = fs_info->LastFreeCluster;
    SizeFree = ((u64)free_clusters * (u64)cluster_size);

    SizeUsed = SizeInBytes - SizeFree;
    fat_lba = Partition_lba + reserved_sectors;
    dir_place = new u8[cluster_size];

    root_lba = Cluster_To_LBA(root_cluster);
    RootCluster = root_cluster;

    delete[] sector;
    printf("boot drive 0x%b:         \n", drive->disk_index);
    printf("Filesystem: FAT32, Capacity: %lu MB, Free Space: %lu MB, Used Space: %lu MB\n", (SizeInBytes / 0x100000), (SizeFree / 0x100000), (SizeUsed / 0x100000));

}

u32 BootFileSystem::find_next_cluster(u32 cluster) {
    u32 offset = cluster * 4;
    u32 sector = offset >> 9; // divide by 512
    u32 entry_offset = offset % 512;

    u8 buffer[512];

    drive->read_sectors(fat_lba + sector, 1, buffer);

    u32 next = *(u32 *)(buffer + entry_offset);

    return (next & 0x0FFFFFFF);
}



u32 BootFileSystem::load_clusters(u32 cluster, u32 count, void *buffer) {
    u32 sector_count = (cluster_size + 511) >> 9;
    u32 sectors = ((cluster_size * count) + 511) >> 9;
    u8 *cache = new u8[sectors<<9];
    u8 *cache_org = cache;
    for (u32 i = 0; i < count; i++) {
        u32 lba = Cluster_To_LBA(cluster);

       drive->read_sectors(lba, sector_count, cache);
       cache += cluster_size;
       cluster = find_next_cluster(cluster);
    }


    memcpy(buffer, cache_org, cluster_size*count);
    delete[] cache_org;
    return cluster;
}


Path_ret BootFileSystem::increase_paths(u64 address, char *path) {
    Path_ret ret = {0};

    if (Paths == nullptr || Path_count == 0) {
        Paths = new u64[1];
        Paths[0] = address;
        table = new Paths_table[1];
        table[0].index = 0;
        table[0].str = path;
        Path_count = 1;
        ret.index = 0;
        ret.status = true;
        table[0].file_count = 0;
        table[0].open_files = nullptr;
        return ret;
    }

    u64 paths_size = 8 * Path_count;
    u64 new_size = paths_size + 8;
    Paths = (u64 *)krealloc(Paths, paths_size, new_size);

    if (Paths == nullptr) {
        ret.status = false;
        return ret;
    }

    u64 table_size = sizeof(Paths_table) * Path_count;
    new_size = table_size + sizeof(Paths_table);
    table = (Paths_table *)krealloc(table, table_size, new_size);

    if (table == nullptr) {
        ret.status = false;
        return ret;
    }

    Paths[Path_count] = address;
    table[Path_count].index = Path_count;
    table[Path_count].str = path;
    table[Path_count].open_files = nullptr;
    table[Path_count].file_count = 0;
    ret.index = Path_count;

    Path_count++;
    ret.status = true;
    return ret;
}



bool BootFileSystem::increase_files(Paths_table *table, char *filename, u32 cluster) {
    if (table == nullptr || filename == nullptr)
        return false;

    if (table->open_files == nullptr || table->file_count == 0) {
        table->open_files = new file_cache[1];
        table->open_files->cluster_count = 0;
        table->open_files->cache = nullptr;
        table->open_files->starting_cluster = cluster;
        table->open_files->name = filename;
        table->file_count = 1;
        return true;
    }

    u64 files_size = table->file_count * sizeof(file_cache);
    u64 new_size = files_size + sizeof(file_cache);
    table->open_files = (file_cache *)krealloc(table->open_files, files_size, new_size);
    if (table->open_files == nullptr)
        return false;

    table->open_files[table->file_count].cache = nullptr;
    table->open_files[table->file_count].cluster_count = 0;
    table->open_files[table->file_count].name = filename;
    table->open_files[table->file_count].starting_cluster = cluster;
    return true;
}


static inline u8 count_path(const char *path) {
    u8 count = 0;
    while (*path != '/' && *path != '\0') {
        count++;
        path++;
    }

    return count;
}


static bool utf16le_to_ascii(const void *src, size_t src_units,
                             char *dst, size_t dst_capacity) {
    if (dst == nullptr || dst_capacity == 0)
        return false;

    dst[0] = '\0';
    if (src == nullptr)
        return false;

    const u8 *bytes = (const u8 *)src;
    size_t written = 0;
    for (size_t i = 0; i < src_units; ++i) {
        u16 unit = (u16)bytes[i * 2] | ((u16)bytes[i * 2 + 1] << 8);
        if (unit == 0 || unit == 0xFFFF)
            break;

        if (written == dst_capacity - 1)
            return false;

        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < src_units) {
            u16 next = (u16)bytes[(i + 1) * 2] |
                       ((u16)bytes[(i + 1) * 2 + 1] << 8);
            if (next >= 0xDC00 && next <= 0xDFFF)
                ++i;
        }

        dst[written++] = unit <= 0x7F ? (char)unit : '?';
        dst[written] = '\0';
    }
    return true;
}

static void collect_lfn(const FAT32_LFN *entry, u16 *name) {
    unsigned ordinal = entry->Sequence_number & 0x1F;
    unsigned base = (ordinal - 1) * 13;

    for (unsigned i = 0; i < 5; ++i)
        name[base + i] = entry->first_part[i];

    for (unsigned i = 0; i < 6; ++i)
        name[base + 5 + i] = entry->second_part[i];

    for (unsigned i = 0; i < 2; ++i)
        name[base + 11 + i] = entry->final_part[i];
}


#define FAT32_EOC 0x0FFFFFFF

int BootFileSystem::scan_dir(u32 cluster) {
    int count = 0;

    while (cluster != 0x0FFFFFFF) {
        count++;
        cluster = find_next_cluster(cluster);
    }

    return count;
}


Path_ret BootFileSystem::open_path(const char *path) {
    Path_ret ret = {0};

    u32 backup_cluster = 0;
    char path_name[14];
    u32 cluster = RootCluster;
    cluster = load_clusters(cluster, 1, dir_place);

    u64 size = sizeof(FAT32_DIR);
    u8 *dir_data = dir_place;
    bool started = true;

    FAT32_DIR *dir = (FAT32_DIR *)((u64)dir_data);
    bool matched = true;

    while (path[1] != '\0') {

        start:

        if (started) {
            started = false;
            goto skip_add;
        }
         
        if (((u64)dir_data + size) >= ((u64)dir_place + cluster_size)) {
            cluster = load_clusters(cluster, 1, dir_place);
            dir_data = dir_place;
            size = sizeof(FAT32_DIR);
        } else {
            dir_data += size;
            size = sizeof(FAT32_DIR);
        }

        skip_add:

        if (*dir_data == 0xE5)  // deleted entry
            goto start;
        
        if (*dir_data == 0x00) { // no more entries
            ret.status = false;
            return ret;   
        }

        if (*path != '/' && matched) {
            ret.status = false;
            return ret;
        }
        
        if (matched) {
            path++;
            matched = false;
        }
        
        u8 count = count_path(path);
        if (count == 0) {
            ret.status = false;
            return ret;
        }

        memcpy(path_name, path, count);

        dir = (FAT32_DIR *)((u64)dir_data);
        char *name = nullptr;

        u8 cmp_count = 11;
        if (dir->attributes == LFN) {
            size += sizeof(FAT32_LFN);
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)dir_data + sizeof(FAT32_DIR));
            u16 *utf_name = new u16[13];
            collect_lfn(lfn, utf_name);
            name = new char[14];
            utf16le_to_ascii(utf_name, 13, name, 14);
            delete[] utf_name;
            cmp_count = 13;
        } else {
            name = new char[11];
            for (u8 i = 0; i < 11; i++) 
                name[i] = dir->filename[i];
            
        }

        for (u8 i = count; i < cmp_count; i++)
            path_name[i] = ' ';

        if (memcmp(path_name, name, cmp_count) != 0) {
            delete[] name;
            continue;
        }

        cluster = ((u32)dir->cluster_high << 16) | (u32)dir->cluster_low;
        backup_cluster = cluster;
        cluster = load_clusters(cluster, 1, dir_place);
        dir_data = dir_place;
        delete[] name;
        path += count;
        matched = true;
    }

    int count = scan_dir(backup_cluster);
    size = cluster_size * count;
    u8 *path_cache = new u8[size];
    cluster = backup_cluster;
    load_clusters(cluster, count, path_cache);
    ret = increase_paths((u64)path_cache, (char *)path);
    if (!ret.status) 
        delete[] path_cache;
    return ret;
}


Search_ret BootFileSystem::search_path(char *path) {
    Search_ret ret = {0};

    for (u32 i = 0; i < Path_count; i++) {
        char *str = table[i].str;
        if (strcmp(str, path) == 0) {
            u8 *cache = (u8 *)Paths[i];
            ret.index = i;
            ret.cache = cache;
            return ret;
        }
    }

    ret.cache = nullptr;
    return ret;
}



u64 BootFileSystem::open_file_by_path(const char *path, const char *filename) {
    bool try_again = true;
    again:
    
    Search_ret ret = search_path((char *)path);
    u8 *cache = ret.cache;
    if (cache == nullptr) {
        if (try_again) {
            try_again = false;
            Path_ret ret = open_path(path);
            if (!ret.status)
                return 0xFFFFFFFF;
            goto again;
        }
    } else if (try_again) {
        for (u32 i = 0; i < table[ret.index].file_count; i++) {
            file_cache *file = &table[ret.index].open_files[i];
            if (strcmp((char *)filename, file->name) == 0) {
                u64 file_handle = ((u64)ret.index << 32) | (u64)i;
                return file_handle;
            }
        }
    }

    u64 size = sizeof(FAT32_DIR);
    while (*cache != 0x00) {
        if (*cache == 0xE5) {
            cache += size;
            continue;
        }

        FAT32_DIR *dir = (FAT32_DIR *)((u64)cache);
        u8 count = str_size(filename);
        u8 cmp_count = 11;
        char *name = nullptr;
        char *file = nullptr;


        if (dir->attributes == LFN) {
            size += sizeof(FAT32_LFN);
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)cache + sizeof(FAT32_DIR));
            u16 *utf_name = new u16[13];
            collect_lfn(lfn, utf_name);
            name = new char[14];
            utf16le_to_ascii(utf_name, 13, name, 14);
            delete[] utf_name;
            cmp_count = 13;
        } else {
            name = new char[11];
            for (u8 i = 0; i < 11; i++) 
                name[i] = dir->filename[i];
        }

        file = new char[cmp_count];
        for (u8 i = 0; i < count; i++)
            file[i] = filename[i];

        for (u8 i = count; i < cmp_count; i++)
            file[i] = ' ';

        if (memcmp(file, name, cmp_count) == 0) {
            delete[] name;
            delete[] file;
            u32 cluster = ((u32)dir->cluster_high << 16) | (u32)dir->cluster_low;
            Paths_table *ptr = &table[ret.index];
            if (increase_files(ptr, (char *)filename, cluster)) {
                u64 file_handle = ((u64)ret.index << 32) | ((u64)ptr->file_count - 1);
                return file_handle;
            }
        }

        cache += size;
    }

    return 0xFFFFFFF; // for failure
}




u64 BootFileSystem::open_file_by_index(u32 index, const char *filename) {
    if (index >= Path_count)
        return 0xFFFFFFFF;

    u8 *cache = (u8 *)Paths[index];

    for (u32 i = 0; i < table[index].file_count; i++) {
        if (strcmp((char *)filename, table[index].open_files[i].name) == 0) {
            u64 file_handle = ((u64)index << 32) | ((u64)i);
            return file_handle;
        }
    }

    u64 size = sizeof(FAT32_DIR);
    while (*cache != 0x00) {
        if (*cache == 0xE5) {
            cache += size;
            continue;
        }

        FAT32_DIR *dir = (FAT32_DIR *)((u64)cache);
        u8 count = str_size(filename);
        u8 cmp_count = 11;
        char *name = nullptr;
        char *file = nullptr;


        if (dir->attributes == LFN) {
            size += sizeof(FAT32_LFN);
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)cache + sizeof(FAT32_DIR));
            u16 *utf_name = new u16[13];
            collect_lfn(lfn, utf_name);
            name = new char[14];
            utf16le_to_ascii(utf_name, 13, name, 14);
            delete[] utf_name;
            cmp_count = 13;
        } else {
            name = new char[11];
            for (u8 i = 0; i < 11; i++) 
                name[i] = dir->filename[i];
        }

        file = new char[cmp_count];
        for (u8 i = 0; i < count; i++)
            file[i] = filename[i];

        for (u8 i = count; i < cmp_count; i++)
            file[i] = ' ';

        if (memcmp(file, name, cmp_count) == 0) {
            delete[] name;
            delete[] file;
            u32 cluster = ((u32)dir->cluster_high << 16) | (u32)dir->cluster_low;
            Paths_table *ptr = &table[index];
            if (increase_files(ptr, (char *)filename, cluster)) {
                u64 file_handle = ((u64)index << 32) | ((u64)ptr->file_count - 1);
                return file_handle;
            }
        }

        cache += size;

    }

    return 0xFFFFFFFF; // for failure
}

static inline void remove_file_entry(Paths_table *table, u32 file_index) {
    if (table->open_files == nullptr)
        return;
    
    cache_descriptor *filecache = table->open_files[file_index].cache;
    if (filecache != nullptr) {
        delete[] filecache->cache;
        delete[] filecache;
    }

    for (u32 i = file_index; i < table->file_count; i++) 
        table->open_files[i] = table->open_files[i+1];

    u64 old_size = table->file_count * sizeof(file_cache);
    u64 new_size = old_size - sizeof(file_cache);


    if (new_size == 0) 
        kfree(table->open_files);
    else {
        file_cache *old_pointer = table->open_files;
        table->open_files = (file_cache *)krealloc(table->open_files, old_size, new_size);
        if (table->open_files == nullptr)
            table->open_files = old_pointer;
    }


    table->file_count--;
}


void BootFileSystem::close_file(u64 file_handle) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)(file_handle & 0xFFFFFFFF);

    if (path_index >= Path_count || table == nullptr)
        return;

    if (file_index >= table[path_index].file_count || table[path_index].open_files == nullptr)
        return;

    remove_file_entry(&table[path_index], file_index);
    
}


void BootFileSystem::close_path(u32 path_index) {
    if (path_index >= Path_count || table == nullptr)
        return;

        // close all files on path
   for (u32 i = (table[path_index].file_count - 1); i >= 0; i--) {
        u64 file_handle = ((u64)path_index << 32) | (u64)i;
        close_file(file_handle);
   }

   table[path_index] = {0};
        // relocate paths entries
   for (u32 i = path_index; i < Path_count; i++) 
        table[i] = table[i+1];

   u64 old_size = Path_count * sizeof(Paths_table);
   u64 new_size = old_size - sizeof(Paths_table);

   if (new_size == 0) 
        kfree(table);
    else {
        Paths_table *old_pointer = table;
        table = (Paths_table *)krealloc(table, old_size, new_size);
        if (table == nullptr)
            table = old_pointer;
    }

    Path_count--;

}

void BootFileSystem::allocate_clusters(file_cache *file, u32 cluster_count) {
    cache_descriptor *filecache = file->cache;
    if (filecache == nullptr || file->cluster_count == 0) {
        filecache = new cache_descriptor[1];
        filecache->cluster_index = cluster_count;
        file->cluster_count = cluster_count;
        u64 size = cluster_count * cluster_size;
        filecache->cache = new u8[size];
        file->starting_cluster = load_clusters(file->starting_cluster, cluster_count, filecache->cache);
        file->cache = filecache;
        return;
    }


    u64 cluster_off = filecache->cluster_index * cluster_size;
    filecache->cluster_index += cluster_count;
    u64 old_size = file->cluster_count * cluster_size;
    file->cluster_count += cluster_count;
    u64 new_size = file->cluster_count * cluster_size;

    filecache->cache = (u8 *)krealloc(filecache->cache, old_size, new_size);
    file->cache = filecache;
    if (filecache->cache == nullptr)
        return;
    file->starting_cluster = load_clusters(file->starting_cluster, cluster_count, (void *)(filecache->cache + cluster_off));
}


void BootFileSystem::read_file(u64 file_handle, u64 offset, u32 size, void *buffer) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)file_handle;

    if (path_index >= Path_count || table == nullptr)
        return;

    if (file_index >= table[path_index].file_count || table[path_index].open_files == nullptr)
        return;

    file_cache *file = &table[path_index].open_files[file_index];

    u32 clusters = (offset + size + (cluster_size - 1)) / cluster_size;
    if (clusters > file->cluster_count) 
        allocate_clusters(file, (clusters - file->cluster_count));
    
    memcpy(buffer, (void *)(file->cache->cache+offset), size);
    
}