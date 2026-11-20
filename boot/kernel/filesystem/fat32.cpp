#include "filesystem/fs.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/timers/rtc.h"

// Disk layout: partition boot sector, reserved sectors (including FSInfo), FAT copies,
// then data clusters. This implementation reads and writes 512-byte sectors.


extern BootFileSystem *fat32_fs;

// Packed boot records mirror on-disk byte offsets; padding must not be inserted.
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



// Data cluster numbering begins at 2; clusters 0 and 1 are reserved by FAT.
u32 BootFileSystem::Cluster_To_LBA(u32 cluster) {
    u32 lba = first_data_lba + ((cluster - 2) * SectorsPerCluster); // cluster size would practically almost always be powers of two
    return lba;
}


u32 BootFileSystem::find_next_cluster(u32 cluster) {
    return valid_cluster(cluster) ? cluster_map[cluster] : cluster;
}

bool BootFileSystem::scan_fat() {
    cluster_map = (u32 *)kmalloc((u64)(clusters + 2) * sizeof(u32));
    if (!cluster_map)
        return false;

    u32 sector[128];
    for (u32 first = 0; first < clusters + 2; first += 128) {
        drive->read_sectors(fat_lba + first / 128, 1, sector);
        for (u32 i = 0; i < 128 && first + i < clusters + 2; ++i)
            cluster_map[first + i] = sector[i] & 0x0FFFFFFF;
    }
    return true;
}

// Read volume geometry, validate boot/FSInfo signatures, then prepare caches
// and the cluster map needed by later directory and file operations.
void BootFileSystem::fs_init(disk_drive *boot_drive, BootPartition *partition, u32 lba, u64 size) {
    if (boot_drive == nullptr || lba == 0 || size == 0) {
        printf("BOOT_DRIVE : FAT32: no boot partition found\n");
        return;
    }

    Paths = nullptr;
    table = nullptr;
    Path_count = 0;

    cluster_map = nullptr;
    clusters = 0;

    SizeInBytes = size;
    Partition_lba = lba;
    Partition = partition;
    drive = boot_drive;

    u8 *sector = new u8[512];
    memset(sector, 0, 512);
    boot_drive->read_sectors(lba, 1, sector);

    FAT32_BR *br = (FAT32_BR *)sector;
    FAT32_EBR *ebr = (FAT32_EBR *)(sector + sizeof(FAT32_BR));

    // Save boot-sector fields before reusing the same buffer to read FSInfo.
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

    u32 total_sectors = br->TotalSectors ? br->TotalSectors : br->LargeSectorCount;
    u32 fat_sectors = ebr->SectorsPerFAT;
    u8 fat_count = br->NumOfFATS;
    u8 sectors_per_cluster = br->SectorsPerCluster;
    u64 data_start = reserved_sectors + (u64)fat_count * fat_sectors;
    bool mirrored = !(ebr->Flags & 0x80);
    u8 active_fat = mirrored ? 0 : (ebr->Flags & 0x0F);
    if (!fat_count || !fat_sectors || active_fat >= fat_count || !sectors_per_cluster ||
        data_start >= total_sectors || (u64)total_sectors * 512 > size ||
        (u64)lba + total_sectors > 0x100000000ULL) {
        delete[] sector;
        return;
    }
    u32 count = (total_sectors - data_start) / sectors_per_cluster;
    if (!count || count > 0x0FFFFFF5 || count + 2 > (u64)fat_sectors * 128 ||
        root_cluster < 2 || root_cluster - 2 >= count) {
        delete[] sector;
        return;
    }

    first_data_lba = lba + data_start;
    fat_lba = lba + reserved_sectors + (u32)active_fat * fat_sectors;
    SectorsPerCluster = sectors_per_cluster;
    sectors_per_fat = fat_sectors;
    fat_copies = mirrored ? fat_count : 1;
    clusters = count;

    u32 fs_info_lba = lba + fs_info_sector;
    memset(sector, 0, 512);
    boot_drive->read_sectors(fs_info_lba, 1, sector);
    FAT32_FSinfo *fs_info = (FAT32_FSinfo *)sector;
    bool valid_fs_info = fs_info->LeadSignature == 0x41615252 &&
        fs_info->AnotherSignature == 0x61417272 &&
        fs_info->TrialSignature == 0xAA550000;

    // Try the corresponding FSInfo sector in the backup boot area if available.
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

    // FSInfo supplies a cached free-cluster count; 0xFFFFFFFF means unknown,
    // but this calculation currently assumes the count is available.
    u32 free_clusters = fs_info->LastFreeCluster;
    SizeFree = ((u64)free_clusters * (u64)cluster_size);

    SizeUsed = SizeInBytes - SizeFree;


    root_lba = Cluster_To_LBA(root_cluster);
    RootCluster = root_cluster;

    delete[] sector;
    if (!scan_fat())
        return;
    printf("boot drive 0x%b:         \n", drive->disk_index);
    printf("Filesystem: FAT32, Capacity: %lu MB, Free Space: %lu MB, Used Space: %lu MB\n", (SizeInBytes / 0x100000), (SizeFree / 0x100000), (SizeUsed / 0x100000));

}





// Follow the FAT chain rather than assuming clusters are adjacent on disk.
// Copy count clusters into buffer and return the entry after the last one read.
u32 BootFileSystem::load_clusters(u32 cluster, u32 count, void *buffer) {
    auto *data = static_cast<u8 *>(buffer);
    for (u32 i = 0; i < count && valid_cluster(cluster); ++i) {
        drive->read_sectors(Cluster_To_LBA(cluster), SectorsPerCluster, data);
        data += cluster_size;
        cluster = find_next_cluster(cluster);
    }
    return cluster;
}

u32 BootFileSystem::write_clusters(u32 cluster, u32 count, void *buffer) {
    auto *data = static_cast<u8 *>(buffer);
    for (u32 i = 0; i < count && valid_cluster(cluster); ++i) {
        drive->write_sectors(Cluster_To_LBA(cluster), SectorsPerCluster, data);
        data += cluster_size;
        cluster = find_next_cluster(cluster);
    }
    return cluster;
}

// Keep path and file indices stable while their handles are in use.
Path_ret BootFileSystem::increase_paths(u64 address, char *path, int cluster_count, u32 starting_cluster) {
    u32 index = 0;
    while (index < Path_count && Paths[index])
        ++index;
    if (index == Path_count) {
        auto *paths = (u64 *)krealloc(Paths, Path_count * sizeof(u64),
                                     (Path_count + 1) * sizeof(u64));
        if (!paths)
            return {};
        Paths = paths;
        auto *entries = (Paths_table *)krealloc(table, Path_count * sizeof(Paths_table),
                                               (Path_count + 1) * sizeof(Paths_table));
        if (!entries)
            return {};
        table = entries;
        ++Path_count;
    }
    Paths[index] = address;
    table[index] = {index, path, nullptr, 0, cluster_count, starting_cluster};
    return {true, index};
}

static FAT32_DIR *find_short_entry(void *buffer, u64 bytes, const char *filename,
                                   bool &end_of_directory) {
    auto *entries = static_cast<FAT32_DIR *>(buffer);
    for (u64 i = 0; i < bytes / sizeof(FAT32_DIR); ++i) {
        FAT32_DIR &entry = entries[i];
        u8 first = (u8)entry.filename[0];
        if (first == 0) {
            end_of_directory = true;
            break;
        }
        if (first != 0xE5 && !(entry.attributes & 0x08) &&
            memcmp(filename, entry.filename, sizeof(entry.filename)) == 0)
            return &entry;
    }
    return nullptr;
}

Path_ret BootFileSystem::open_path(const char *path) {
    if (!path || path[0] != '/' || !cluster_size || !cluster_map)
        return {};
    auto *scratch = (u8 *)kmalloc(cluster_size);
    if (!scratch)
        return {};
    u32 cluster = RootCluster;
    const char *component = path + 1;
    while (*component) {
        char name[11];
        memset(name, ' ', sizeof(name));
        unsigned length = 0;
        while (length < sizeof(name) && component[length] && component[length] != '/') {
            name[length] = component[length];
            ++length;
        }
        if (!length || (component[length] && component[length] != '/')) {
            kfree(scratch);
            return {};
        }

        FAT32_DIR *entry = nullptr;
        bool end = false;
        u32 cursor = cluster;
        for (u32 i = 0; i < clusters && valid_cluster(cursor) && !end; ++i) {
            drive->read_sectors(Cluster_To_LBA(cursor), SectorsPerCluster, scratch);
            entry = find_short_entry(scratch, cluster_size, name, end);
            if (entry)
                break;
            cursor = find_next_cluster(cursor);
        }
        if (!entry || !(entry->attributes & 0x10)) {
            kfree(scratch);
            return {};
        }
        cluster = ((u32)entry->cluster_high << 16) | entry->cluster_low;
        component += length;
        if (*component == '/')
            ++component;
    }
    kfree(scratch);

    u32 cursor = cluster;
    u32 count = 0;
    while (valid_cluster(cursor) && count < clusters) {
        ++count;
        cursor = find_next_cluster(cursor);
    }
    if (!count || cursor < 0x0FFFFFF8)
        return {};

    auto *cache = (u8 *)kmalloc((u64)count * cluster_size);
    u64 path_bytes = component - path + 1;
    auto *name = (char *)kmalloc(path_bytes);
    if (!cache || !name) {
        if (cache)
            kfree(cache);
        if (name)
            kfree(name);
        return {};
    }
    memcpy(name, path, path_bytes);
    load_clusters(cluster, count, cache);
    Path_ret result = increase_paths((u64)cache, name, count, cluster);
    if (!result.status) {
        kfree(cache);
        kfree(name);
    }
    return result;
}

bool BootFileSystem::valid_cluster(u32 cluster) const {
    return cluster >= 2 && cluster - 2 < clusters;
}

// Boot callers may use unpadded components, while syscalls use padded names.
static bool same_path(const char *left, const char *right) {
    while (*left || *right) {
        if (*left == *right) {
            ++left;
            ++right;
            continue;
        }
        while (*left == ' ')
            ++left;
        while (*right == ' ')
            ++right;
        if ((*left && *left != '/') || (*right && *right != '/'))
            return false;
        if (*left == '/')
            ++left;
        if (*right == '/')
            ++right;
    }
    return true;
}

Path_ret BootFileSystem::find_open_path(const char *path) const {
    if (!path)
        return {};
    for (u32 i = 0; i < Path_count; ++i) {
        if (Paths[i] && same_path(table[i].str, path))
            return {true, i};
    }
    return {};
}

FAT32_DIR *BootFileSystem::find_entry(u32 index, const char *filename) const {
    if (index >= Path_count || !Paths[index] || !filename)
        return nullptr;
    char name[11];
    memset(name, ' ', sizeof(name));
    unsigned length = 0;
    while (length < sizeof(name) && filename[length]) {
        name[length] = filename[length];
        ++length;
    }
    if (!length || filename[length])
        return nullptr;
    bool end = false;
    return find_short_entry((void *)Paths[index], (u64)table[index].cluster_count * cluster_size,
                            name, end);
}

bool BootFileSystem::lookup_entry(u32 index, const char *filename, FAT32_DIR &entry) const {
    const FAT32_DIR *found = find_entry(index, filename);
    if (!found)
        return false;
    entry = *found;
    const Paths_table &parent = table[index];
    u64 offset = (u64)found - Paths[index];
    for (int i = 0; i < parent.file_count; ++i) {
        const file_cache &file = parent.open_files[i];
        if (file.references && file.directory_offset == offset) {
            entry.file_size = file.file_size;
            break;
        }
    }
    return true;
}

u64 BootFileSystem::open_file_by_path(const char *path, const char *filename) {
    Path_ret cached = find_open_path(path);
    if (cached.status)
        return open_file_by_index(cached.index, filename);
    Path_ret opened = open_path(path);
    if (!opened.status)
        return 0xFFFFFFFF;
    u64 handle = open_file_by_index(opened.index, filename);
    if (handle == 0xFFFFFFFF)
        close_path(opened.index);
    return handle;
}

int BootFileSystem::reserve_file_slot(Paths_table &parent) {
    int count = parent.file_count;
    for (int i = 0; i < count; ++i) {
        if (!parent.open_files[i].references)
            return i;
    }
    auto *files = (file_cache *)krealloc(parent.open_files, count * sizeof(file_cache),
                                       (count + 1) * sizeof(file_cache));
    if (!files)
        return -1;
    parent.open_files = files;
    ++parent.file_count;
    return count;
}

u64 BootFileSystem::open_file_by_index(u32 index, const char *filename) {
    const FAT32_DIR *entry = find_entry(index, filename);
    if (!entry || (entry->attributes & 0x10))
        return 0xFFFFFFFF;

    Paths_table &parent = table[index];
    u64 offset = (u64)entry - Paths[index];
    for (int i = 0; i < parent.file_count; ++i) {
        file_cache &file = parent.open_files[i];
        if (file.references && file.directory_offset == offset) {
            ++file.references;
            return ((u64)index << 32) | (u32)i;
        }
    }

    int slot = reserve_file_slot(parent);
    if (slot < 0)
        return 0xFFFFFFFF;

    u32 cluster = ((u32)entry->cluster_high << 16) | entry->cluster_low;
    parent.open_files[slot] = {cluster, 0, nullptr, 0, offset, entry->file_size, false, 1};
    return ((u64)index << 32) | (u32)slot;
}

file_cache *BootFileSystem::get_open_file(u64 handle) const {
    u32 path = handle >> 32;
    u32 index = (u32)handle;
    if (path >= Path_count || !Paths[path] || index >= (u32)table[path].file_count)
        return nullptr;
    file_cache &file = table[path].open_files[index];
    return file.references ? &file : nullptr;
}

static void update_entry_metadata(FAT32_DIR &entry, u32 size);

void BootFileSystem::close_file(u64 handle) {
    file_cache *file = get_open_file(handle);
    if (!file)
        return;
    if (file->dirty) {
        u32 path = handle >> 32;
        Paths_table &parent = table[path];
        auto *entry = (FAT32_DIR *)(Paths[path] + file->directory_offset);
        write_clusters(file->starting_cluster, file->cluster_count, file->cache);
        entry->cluster_high = file->starting_cluster >> 16;
        entry->cluster_low = (u16)file->starting_cluster;
        update_entry_metadata(*entry, file->file_size);
        write_clusters(parent.starting_cluster, parent.cluster_count, (void *)Paths[path]);
        file->dirty = false;
    }
    if (--file->references == 0) {
        if (file->cache)
            kfree(file->cache);
        *file = {};
    }
}

void BootFileSystem::close_path(u32 index) {
    if (index >= Path_count || !Paths[index])
        return;
    Paths_table &parent = table[index];
    for (int i = 0; i < parent.file_count; ++i) {
        while (parent.open_files[i].references)
            close_file(((u64)index << 32) | (u32)i);
    }
    if (parent.open_files)
        kfree(parent.open_files);
    kfree(parent.str);
    kfree((void *)Paths[index]);
    Paths[index] = 0;
    parent = {};
}

bool BootFileSystem::ensure_file_clusters(file_cache &file, u32 count, bool extend) {
    if (count <= (u32)file.cluster_count)
        return true;
    if (count > clusters)
        return false;

    u32 existing = file.cluster_count;
    u32 next = existing ? find_next_cluster(file.last_cluster) : file.starting_cluster;
    while (existing < count && valid_cluster(next)) {
        ++existing;
        next = find_next_cluster(next);
    }
    if (existing < count) {
        if (!extend || (next && next < 0x0FFFFFF8))
            return false;
        u32 needed = count - existing;
        for (u32 i = 2; i < clusters + 2 && needed; ++i) {
            if (!cluster_map[i])
                --needed;
        }
        if (needed)
            return false;
    }

    auto *buffer = (u8 *)krealloc(file.cache, (u64)file.cluster_count * cluster_size,
                                 (u64)count * cluster_size);
    if (!buffer)
        return false;
    file.cache = buffer;
    u32 free_hint = 2;
    while ((u32)file.cluster_count < count) {
        u32 cluster = file.cluster_count ? find_next_cluster(file.last_cluster) : file.starting_cluster;
        if (!valid_cluster(cluster)) {
            if (!extend || (cluster && cluster < 0x0FFFFFF8))
                return false;
            cluster = find_free_cluster(free_hint);
            if (!cluster)
                return false;
            free_hint = cluster + 1;
            set_fat_entry(cluster, 0x0FFFFFFF);
            if (file.cluster_count)
                set_fat_entry(file.last_cluster, cluster);
            else
                file.starting_cluster = cluster;
            file.dirty = true;
        } else {
            drive->read_sectors(Cluster_To_LBA(cluster), SectorsPerCluster,
                                buffer + (u64)file.cluster_count * cluster_size);
        }
        file.last_cluster = cluster;
        ++file.cluster_count;
    }
    return true;
}

bool BootFileSystem::read_file(u64 handle, u64 offset, u32 size, void *buffer) {
    if (!size)
        return true;
    file_cache *file = get_open_file(handle);
    if (!file || !buffer || offset > 0xFFFFFFFFULL - size)
        return false;
    u32 count = (offset + size + cluster_size - 1) / cluster_size;
    if (!ensure_file_clusters(*file, count, false))
        return false;
    memcpy(buffer, file->cache + offset, size);
    return true;
}

bool BootFileSystem::write_file(u64 handle, u64 offset, u32 size, const void *buffer) {
    if (!size)
        return true;
    file_cache *file = get_open_file(handle);
    if (!file || !buffer || offset > 0xFFFFFFFFULL - size)
        return false;
    u32 count = (offset + size + cluster_size - 1) / cluster_size;
    if (!ensure_file_clusters(*file, count, true))
        return false;
    if (offset > file->file_size)
        memset(file->cache + file->file_size, 0, offset - file->file_size);
    memcpy(file->cache + offset, buffer, size);
    if (offset + size > file->file_size)
        file->file_size = offset + size;
    file->dirty = true;
    return true;
}

bool BootFileSystem::truncate_file(u64 handle) {
    file_cache *file = get_open_file(handle);
    if (!file)
        return false;
    u32 path = handle >> 32;
    Paths_table &parent = table[path];
    auto *entry = (FAT32_DIR *)(Paths[path] + file->directory_offset);
    entry->cluster_high = 0;
    entry->cluster_low = 0;
    update_entry_metadata(*entry, 0);
    write_clusters(parent.starting_cluster, parent.cluster_count, (void *)Paths[path]);

    u32 cluster = file->starting_cluster;
    for (u32 i = 0; i < clusters && valid_cluster(cluster); ++i) {
        u32 next = find_next_cluster(cluster);
        set_fat_entry(cluster, 0);
        cluster = next;
    }
    if (file->cache)
        kfree(file->cache);
    file->cache = nullptr;
    file->starting_cluster = 0;
    file->last_cluster = 0;
    file->cluster_count = 0;
    file->file_size = 0;
    file->dirty = false;
    return true;
}

void BootFileSystem::set_fat_entry(u32 cluster, u32 value) {
    u32 sector[128];
    u32 offset = cluster % 128;
    for (u8 copy = 0; copy < fat_copies; ++copy) {
        u32 lba = fat_lba + copy * sectors_per_fat + cluster / 128;
        drive->read_sectors(lba, 1, sector);
        sector[offset] = (sector[offset] & 0xF0000000) | value;
        drive->write_sectors(lba, 1, sector);
    }
    cluster_map[cluster] = value;
}

u32 BootFileSystem::find_free_cluster(u32 start) {
    for (u32 cluster = start; cluster < clusters + 2; ++cluster) {
        if (!cluster_map[cluster])
            return cluster;
    }
    return 0;
}

// Decode packed BCD from the RTC; assumes the RTC is operating in BCD mode.
static inline u8 rtc_to_num(u8 n) {
    n = n - ((n >> 4) * 6); // Subtract six per upper-nibble digit to convert BCD to binary.
    return n; // Return the decoded RTC field.
}

static void update_entry_metadata(FAT32_DIR &entry, u32 size) {
    u8 min = rtc_to_num(rtc_read(0x02));
    u8 hour = rtc_to_num(rtc_read(0x04));
    u8 day = rtc_to_num(rtc_read(0x07));
    u8 month = rtc_to_num(rtc_read(0x08));
    u8 seconds = rtc_to_num(rtc_read(0x00));
    unsigned year = rtc_to_num(rtc_read(0x09));
    year = year >= 80 ? year - 80 : year + 20;

    entry.last_modify_time = (hour << 11) | (min << 5) | (seconds / 2);
    entry.last_modify_date = (year << 9) + (month << 5) + (day & 0x1F);
    entry.file_size = size;
}

bool BootFileSystem::fat32_make_entry(u32 index, const char *filename, bool read_only) {
    if (index >= Path_count || !Paths[index] || !filename ||
        str_size(filename) != 11 || find_entry(index, filename))
        return false;

    Paths_table &parent = table[index];
    if (reserve_file_slot(parent) < 0)
        return false;
    u64 bytes = (u64)parent.cluster_count * cluster_size;
    u64 offset = 0;
    while (offset < bytes) {
        u8 first = *(u8 *)(Paths[index] + offset);
        if (first == 0 || first == 0xE5)
            break;
        offset += sizeof(FAT32_DIR);
    }
    if (offset == bytes) {
        u32 cluster = find_free_cluster();
        if (!cluster)
            return false;
        auto *buffer = krealloc((void *)Paths[index], bytes, bytes + cluster_size);
        if (!buffer)
            return false;
        u32 tail = parent.starting_cluster;
        for (int i = 1; i < parent.cluster_count; ++i)
            tail = find_next_cluster(tail);
        set_fat_entry(cluster, 0x0FFFFFFF);
        // Initialize the new cluster before making it reachable.
        drive->write_sectors(Cluster_To_LBA(cluster), SectorsPerCluster, (u8 *)buffer + bytes);
        set_fat_entry(tail, cluster);
        Paths[index] = (u64)buffer;
        ++parent.cluster_count;
    }
    auto *entry = (FAT32_DIR *)(Paths[index] + offset);
    bool end = entry->filename[0] == 0;
    *entry = {};
    memcpy(entry->filename, filename, sizeof(entry->filename));
    entry->attributes = read_only ? 0x01 : 0x20;
    update_entry_metadata(*entry, 0);
    entry->Time = entry->last_modify_time;
    entry->Date = entry->last_modify_date;
    if (end && offset + sizeof(FAT32_DIR) < (u64)parent.cluster_count * cluster_size)
        *(u8 *)(Paths[index] + offset + sizeof(FAT32_DIR)) = 0;
    write_clusters(parent.starting_cluster, parent.cluster_count, (void *)Paths[index]);
    return true;
}

// ASCII 8.3 names only. The caller owns the returned new[] buffer.
char *convert_str_to_fat32(const char *str) {
    if (str == nullptr || *str == '\0')
        return nullptr;

    unsigned base = 0, extension = 0;
    bool dot = false;
    for (const char *p = str; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '.') {
            if (dot || base == 0)
                return nullptr;
            dot = true;
            continue;
        }
        // Restrict this initial encoder to printable ASCII FAT short names.
        if (ch <= 0x20 || ch >= 0x7F || ch == '"' || ch == '*' ||
            ch == '+' || ch == ',' || ch == '/' || ch == ':' ||
            ch == ';' || ch == '<' || ch == '=' || ch == '>' ||
            ch == '?' || ch == '[' || ch == '\\' || ch == ']' || ch == '|')
            return nullptr;
        if (dot) {
            if (++extension > 3)
                return nullptr;
        } else if (++base > 8) {
            return nullptr;
        }
    }
    if (dot && extension == 0)
        return nullptr;

    char *dst = new char[12];
    memset(dst, ' ', 11);
    dst[11] = '\0'; // Convenience terminator, not part of the disk field.
    unsigned pos = 0;
    for (const char *p = str; *p; ++p) {
        if (*p == '.') {
            pos = 8;
            continue;
        }
        char ch = *p;
        dst[pos++] = ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
    }
    return dst;
}

// Input is exactly the 11-byte short-name field; no terminator is required.
// Directory attributes and long filenames are handled by the caller.
char *convert_fat32_to_str(const char *fat32) {
    if (fat32 == nullptr)
        return nullptr;
    unsigned base = 8, extension = 3;
    while (base && fat32[base - 1] == ' ')
        --base;
    while (extension && fat32[8 + extension - 1] == ' ')
        --extension;

    char *dst = new char[base + (extension ? extension + 1 : 0) + 1];
    unsigned pos = 0;
    for (unsigned i = 0; i < base; ++i)
        dst[pos++] = fat32[i];
    // FAT encodes an initial 0xE5 as 0x05 in a live short entry.
    if (base && (unsigned char)dst[0] == 0x05)
        dst[0] = (char)0xE5;
    if (extension) {
        dst[pos++] = '.';
        for (unsigned i = 0; i < extension; ++i)
            dst[pos++] = fat32[8 + i];
    }
    dst[pos] = '\0';
    return dst;
}


void BootFileSystem::list_path(u32 index) {
    if (index >= Path_count || !Paths[index])
        return;

    u8 *cache = (u8 *)Paths[index];

    u64 size = sizeof(FAT32_DIR);
    const u8 *end = cache + (u64)table[index].cluster_count * cluster_size;
    while (cache < end && *cache != 0x00) {
        if (*cache == 0xE5) {
            cache += size;
            continue;
        }

        FAT32_DIR *dir = (FAT32_DIR *)((u64)cache);
        char *name = nullptr;


        cache += size;
        if (dir->attributes & 0x08)
            continue;
        name = convert_fat32_to_str(dir->filename);
        printf("%s%s  ", name, (dir->attributes & 0x10) ? "/" : "");
        delete[] name;
    }

    return; // for failure
}



extern "C" void ls(char *path) {
    if (fat32_fs == nullptr)
        return;

    Path_ret ret = fat32_fs->open_path(path);
    if (!ret.status) {
        printf("FAT32: failed to open path %s\n", path);
        return;
    }

    fat32_fs->list_path(ret.index);

    fat32_fs->close_path(ret.index);
}

u64 BootFileSystem::get_file_handle(const char *path, const char *filename) {
    if (!path || !filename)
        return 0xFFFFFFFF;
    Path_ret parent = find_open_path(path);
    if (!parent.status)
        return 0xFFFFFFFF;
    const FAT32_DIR *entry = find_entry(parent.index, filename);
    if (!entry)
        return 0xFFFFFFFF;
    const Paths_table &directory = table[parent.index];
    u64 offset = (u64)entry - Paths[parent.index];
    for (int i = 0; i < directory.file_count; ++i) {
        const file_cache &file = directory.open_files[i];
        if (file.references && file.directory_offset == offset)
            return ((u64)parent.index << 32) | (u32)i;
    }
    return 0xFFFFFFFF;
}

FAT32_DIR BootFileSystem::get_file_dir(u64 handle) const {
    const file_cache *file = get_open_file(handle);
    if (!file)
        return {};
    FAT32_DIR entry = *(const FAT32_DIR *)(Paths[handle >> 32] + file->directory_offset);
    entry.file_size = file->file_size;
    return entry;
}

char *BootFileSystem::return_str(u32 path_index) {
    return path_index < Path_count && Paths[path_index] ? table[path_index].str : nullptr;
}

u64 BootFileSystem::get_filesize(u64 handle) {
    const file_cache *file = get_open_file(handle);
    return file ? file->file_size : 0;
}
