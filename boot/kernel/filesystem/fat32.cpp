#include "filesystem/fs.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/timers/rtc.h"

// Disk layout: partition boot sector, reserved sectors (including FSInfo), FAT copies,
// then data clusters. This implementation reads and writes 512-byte sectors.
#define LFN 0x0F


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


// Integer division maps any sector within a cluster to that cluster number.
u32 BootFileSystem::LBA_to_Cluster(u32 lba) {
    u32 cluster = 2 + (lba - first_data_lba) / SectorsPerCluster;
    return cluster; 
}

// Shared scratch buffer for one directory cluster, reused while traversing paths.
u8 *dir_place = nullptr;




// Each FAT entry occupies four bytes. Locate its sector and byte offset, then
// keep only the low 28 bits: the upper four bits are reserved.
u32 BootFileSystem::find_next_cluster(u32 cluster) {
    if (cluster == 0 || (cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF))
        return cluster;

    u32 offset = cluster * 4;
    u32 sector = offset >> 9; // divide by 512
    u32 entry_offset = offset % 512;

    u8 buffer[512];

    drive->read_sectors(fat_lba + sector, 1, buffer);

    u32 next = *(u32 *)(buffer + entry_offset);

    return (next & 0x0FFFFFFF);
}


// Build an in-memory view of cluster chains, including reverse links.
// handles marks entries already visited through a chain to avoid rescanning them.
void BootFileSystem::scan_fat(void) { // Build forward and backward cluster links from the on-disk FAT.
    cluster_map = new FAT32_cluster[clusters+2]; // Allocate cluster records, leaving slots 0 and 1 for reserved cluster numbers.
    bool *handles = new bool[clusters+2]; // Track which clusters have already been reached while walking a chain.
    memset(handles, 0, clusters+2); // Initially mark every cluster as unvisited.
    for (u32 i = 2; i < (clusters + 2); i++) { // Visit data-cluster indices, skipping reserved entries 0 and 1.
       
        if (handles[i]) // Check whether an earlier chain walk already populated this entry.
            continue; // Skip entries already handled by that walk.


        u32 fat_value = find_next_cluster(i); // Read this cluster's next-cluster value or FAT status marker.
        cluster_map[i].fat_value = fat_value; // Store the raw 28-bit FAT value in the cluster record.
        cluster_map[i].next_cluster = nullptr; // Initialize the forward pointer until a successor is discovered.
        cluster_map[i].next_cluster_index = 0; // Use zero to indicate no recorded successor index.
        cluster_map[i].previous_cluster = nullptr; // Initialize the reverse pointer; this entry has no recorded predecessor yet.
        cluster_map[i].previous_cluster_index = 0; // Use zero to indicate no recorded predecessor index.

        // Zero means free, 0x0FFFFFF7 means bad, and 0x0FFFFFF8..F means end of chain.
        if (fat_value == 0 || fat_value == 0x0FFFFFF7 || // Treat free and bad-cluster markers as chain stops.
            (fat_value >= 0x0FFFFFF8 && fat_value <= 0x0FFFFFFF)) // Also stop for any FAT32 end-of-chain marker in this range.
                continue; // Leave terminal entries without walking a successor.
        else { // Walk the chain when the value is treated as a successor index.
            u32 former_fat = i; // Remember the cluster whose forward link is being constructed.
            while (fat_value != 0 && fat_value != 0x0FFFFFF7 && // Continue while the value is neither free nor a bad-cluster marker.
            !(fat_value >= 0x0FFFFFF8 && fat_value <= 0x0FFFFFFF)) { // Exclude end-of-chain markers before using the value as an array index.
                cluster_map[former_fat].next_cluster = &cluster_map[fat_value]; // Point the previous cluster record at its successor record.
                cluster_map[former_fat].next_cluster_index = fat_value; // Store the successor's numeric cluster index as well.
                cluster_map[fat_value].previous_cluster = &cluster_map[former_fat]; // Point the successor back at the cluster that leads to it.
                cluster_map[fat_value].previous_cluster_index = former_fat; // Store that predecessor's numeric cluster index.
                former_fat = fat_value; // Advance the chain cursor to the successor just linked.
                fat_value = find_next_cluster(fat_value); // Read the new cursor's FAT entry for the next iteration.
                cluster_map[former_fat].fat_value = fat_value; // Save this cluster's own next-cluster value or terminal marker.
                cluster_map[former_fat].next_cluster = nullptr; // Initialize its forward pointer before possibly linking another successor.
                cluster_map[former_fat].next_cluster_index = 0; // Initialize its forward index to indicate no successor yet.
                handles[former_fat] = true; // Mark this cluster so the outer loop will not scan it again.
            } // Finish this chain walk when a terminal value is reached.
        } // Finish processing the nonterminal starting entry.

        handles[i] = true; // Mark the original starting cluster as visited too.
    } // Continue with the next cluster index.
} // Finish building the cluster map.

// Read volume geometry, validate boot/FSInfo signatures, then prepare caches
// and the cluster map needed by later directory and file operations.
void BootFileSystem::fs_init(disk_drive *boot_drive, BootPartition *partition, u32 lba, u64 size) {
    if (boot_drive == nullptr || lba == 0 || size == 0) {
        printf("BOOT_DRIVE : FAT32: no boot partition found\n");
        return;
    }

    Paths = nullptr;
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

    // The data region starts after all reserved sectors and every FAT copy.
    first_data_lba = lba + reserved_sectors + ((u32)br->NumOfFATS * ebr->SectorsPerFAT);
    NumOfFATS = br->NumOfFATS;
    SectorsPerCluster = br->SectorsPerCluster;
    RootEntries = br->RootEntries;

    clusters = (br->SectorsPerFAT * 512) >> 2; // divide by 4

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
    fat_lba = Partition_lba + reserved_sectors;
    dir_place = new u8[cluster_size];

    root_lba = Cluster_To_LBA(root_cluster);
    RootCluster = root_cluster;

    delete[] sector;
    scan_fat();
    printf("boot drive 0x%b:         \n", drive->disk_index);
    printf("Filesystem: FAT32, Capacity: %lu MB, Free Space: %lu MB, Used Space: %lu MB\n", (SizeInBytes / 0x100000), (SizeFree / 0x100000), (SizeUsed / 0x100000));

}





// Follow the FAT chain rather than assuming clusters are adjacent on disk.
// Copy count clusters into buffer and return the entry after the last one read.
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


u32 BootFileSystem::write_clusters(u32 cluster, u32 count, void *buffer) {
    u32 sector_count = (cluster_size + 511) >> 9;
    u32 sectors = ((cluster_size * count) + 511) >> 9;
    u8 *cache = new u8[sectors<<9];
    u8 *cache_org = cache;
    memcpy(cache, buffer, (count*cluster_size));
    for (u32 i = 0; i < count; i++) {
        u32 lba = Cluster_To_LBA(cluster);

       drive->write_sectors(lba, sector_count, cache);
       cache += cluster_size;
       cluster = find_next_cluster(cluster);
       if (cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF) {
            delete[] cache_org;
            return cluster;
       }
    }

    delete[] cache_org;
    return cluster;
}

// Paths stores directory-cache addresses; table stores metadata at matching indices.
// Append both together so the returned index identifies the cached directory.
Path_ret BootFileSystem::increase_paths(u64 address, char *path, int cluster_count, u32 starting_cluster) {
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
        table[0].cluster_count = cluster_count;
        table[0].starting_cluster = starting_cluster;
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
    table[Path_count].cluster_count = cluster_count;
    table[Path_count].starting_cluster = starting_cluster;
    ret.index = Path_count;

    Path_count++;
    ret.status = true;
    return ret;
}



// Register a file under its cached parent directory. File contents are loaded
// lazily by read_file; here only the name and first disk cluster are recorded.
bool BootFileSystem::increase_files(Paths_table *table, char *filename, u32 cluster) {
    if (table == nullptr || filename == nullptr)
        return false;

    if (table->open_files == nullptr || table->file_count == 0) {
        table->open_files = new file_cache[1];
        table->open_files->cluster_count = 0;
        table->open_files->cache = nullptr;
        table->open_files->starting_cluster = cluster;
        table->open_files->last_cluster = cluster;
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
    table->open_files[table->file_count].last_cluster = cluster;
    table->file_count++;
    return true;
}



// Cache sizes are expressed in clusters; krealloc receives byte counts.
// The value 1 is used here as a failure sentinel rather than a valid address.
u64 BootFileSystem::resize_cache(u64 address, u64 old_count, u64 new_count) {
    if (Paths == nullptr || table == nullptr || old_count == 0 || new_count == 0)
        return 1;

    u64 old_size = old_count * cluster_size;
    u64 new_size = new_count * cluster_size;
    u8 *cache = (u8 *)address;
    cache = (u8 *)krealloc(cache, old_size, new_size);
    if (cache == nullptr)
        return 1;


    return (u64)cache;
}


// Measure one slash-delimited component without consuming the separator.
static inline u8 count_path(const char *path) {
    u8 count = 0;
    while (*path != '/' && *path != '\0') {
        count++;
        path++;
    }

    return count;
}


// Decode little-endian UTF-16 explicitly, stopping at a terminator or LFN padding.
// Non-ASCII characters become ?; a valid surrogate pair produces one replacement.
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

// LFN ordinals are one-based; each entry contributes 13 UTF-16 code units
// split across three fields. The caller must provide space for the ordinal.
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

// Count directory clusters by following links. This loop currently recognizes
// only 0x0FFFFFFF, although FAT32 also permits other end-of-chain markers.
int BootFileSystem::scan_dir(u32 cluster) {
    int count = 0;

    while (cluster < 0x0FFFFFF8) {
        count++;
        cluster = find_next_cluster(cluster);
    }

    return count;
}


// Advance count links; if that lands on the exact EOC marker used here,
// walk again to return the preceding data cluster instead of the marker.
u32 BootFileSystem::return_last_cluster(u32 cluster, int count) {
    u32 cluster_org = cluster;
    for (int i = 0; i < count; i++) 
        cluster = find_next_cluster(cluster);
 
    if (cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF) {
        cluster = cluster_org;
        for (int i = 0; i < (count-1); i++)
            cluster = find_next_cluster(cluster);
        return cluster;
    }

    return cluster;
}


// Resolve slash-separated components from the root using the scratch buffer.
// Once resolved, cache the complete target directory chain and register its index.
Path_ret BootFileSystem::open_path(const char *path) { // Resolve directory components from the root and register a directory cache.
    Path_ret ret = {0}; // Initialize the result with failure status until a cache is registered.

    u32 backup_cluster = RootCluster; // Remember the first cluster of the most recently matched directory.
    char path_name[14]; // Temporary component buffer; the copy below currently has no length guard.
    u32 cluster = RootCluster; // Begin traversal at the root directory.
    cluster = load_clusters(cluster, 1, dir_place); // Read one directory cluster; retain the next FAT link as the traversal cursor.

    u64 size = sizeof(FAT32_DIR); // Start with one standard directory-entry stride.
    u8 *dir_data = dir_place; // Point at the first entry in the shared scratch buffer.
    bool started = true; // Prevent advancing past the first entry on the initial iteration.

    FAT32_DIR *dir = (FAT32_DIR *)((u64)dir_data); // Interpret the current bytes as a packed directory entry.
    bool matched = true; // Require a slash before consuming the first path component.

    while (path[1] != '\0') { // Continue while the character after the current path pointer is not the terminator.

        // size is the byte stride for the previous candidate entry. On reaching
        // the buffer boundary, load the next cluster in the directory chain.
        start: // Entry-advance target used when skipping deleted entries.

        if (started) { // Special-case the first entry so it is examined before any advance.
            started = false; // Use normal advancement on subsequent iterations.
            goto skip_add; // Jump directly to examining the current entry.
        } // End this block.
         
        if (((u64)dir_data + size) >= ((u64)dir_place + cluster_size)) { // Check whether advancing would reach or pass the scratch-buffer boundary.
            cluster = load_clusters(cluster, 1, dir_place); // Read one directory cluster; retain the next FAT link as the traversal cursor.
            dir_data = dir_place; // Reset the entry pointer to the beginning of the loaded directory cluster.
            size = sizeof(FAT32_DIR); // Reset the candidate stride to one standard entry.
        } else { // Handle the alternative branch of the preceding condition.
            dir_data += size; // Advance past the previous candidate within the current cluster.
            size = sizeof(FAT32_DIR); // Reset the candidate stride to one standard entry.
        } // End this block.

        skip_add: // Examine the current candidate after any required advancement.

        if (*dir_data == 0xE5) // Skip a deleted directory slot.
            goto start; // Advance to another entry without consuming the path component.
        
        if (*dir_data == 0x00) { // A zero first byte marks the end of the directory entries.
            ret.status = false; // Report that path resolution failed.
            return ret; // Return the current status and registered index, if available.
        } // End this block.

        if (*path != '/' && matched) { // Require a separator when beginning a new component.
            ret.status = false; // Report that path resolution failed.
            return ret; // Return the current status and registered index, if available.
        } // End this block.
        
        if (matched) { // Consume the separator only after the preceding component matched.
            path++; // Move past the slash to the next component.
            matched = false; // Keep searching entries for this component until a match is found.
        } // End this block.
        
        u8 count = count_path(path); // Measure bytes up to the next slash or string terminator.
        if (count == 0) { // Reject an empty path component.
            ret.status = false; // Report that path resolution failed.
            return ret; // Return the current status and registered index, if available.
        } // End this block.

        memcpy(path_name, path, count); // Copy the component for a fixed-width comparison; no terminator is needed.

        dir = (FAT32_DIR *)((u64)dir_data); // Refresh the directory-entry view at the current candidate.
        char *name = nullptr; // Hold a temporary candidate name for comparison.

        // Short names use the raw, space-padded 11-byte field. The LFN branch
        // below handles a single 13-unit fragment rather than a full LFN chain.
        u8 cmp_count = 11; // Compare short names using the raw 11-byte FAT name field.
        if (dir->attributes == LFN) { // Use the limited long-name decoding branch for an LFN attribute.
            size += sizeof(FAT32_LFN); // Include one extra LFN record in the next candidate stride.
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)dir_data + sizeof(FAT32_DIR)); // Read the following record as LFN data, as this implementation currently does.
            u16 *utf_name = new u16[13]; // Allocate space for one 13-unit fragment; higher ordinals need more space.
            collect_lfn(lfn, utf_name); // Place the fragment using its ordinal within the long name.
            name = new char[14]; // Reserve 13 ASCII characters plus a string terminator.
            utf16le_to_ascii(utf_name, 13, name, 14); // Convert this fragment to ASCII, replacing non-ASCII characters with ?.
            delete[] utf_name; // Release the temporary UTF-16 fragment buffer.
            cmp_count = 13; // Use the fragment width for the upcoming comparison.
        } else { // Handle the alternative branch of the preceding condition.
            name = new char[11]; // Allocate the raw short-name field without a string terminator.
            for (u8 i = 0; i < 11; i++) // Copy all bytes of the fixed-width short name.
                name[i] = dir->filename[i]; // Preserve the on-disk character or padding byte.
            
        } // End this block.

        for (u8 i = count; i < cmp_count; i++) // Fill the unused portion of the requested component to comparison width.
            path_name[i] = ' '; // Pad with spaces to match the short-name representation.

        if (memcmp(path_name, name, cmp_count) != 0) { // Compare candidate and requested component byte for byte.
            delete[] name; // Release the temporary candidate-name buffer.
            continue; // Try the next directory candidate for the same component.
        } // End this block.

        // Directory entries split their starting cluster between two 16-bit fields.
        cluster = ((u32)dir->cluster_high << 16) | (u32)dir->cluster_low; // Combine the two halves of the matched entry's starting cluster.
        backup_cluster = cluster; // Save the directory head before load_clusters advances the cursor.
        cluster = load_clusters(cluster, 1, dir_place); // Read one directory cluster; retain the next FAT link as the traversal cursor.
        dir_data = dir_place; // Reset the entry pointer to the beginning of the loaded directory cluster.
        delete[] name; // Release the temporary candidate-name buffer.
        path += count; // Move past the matched component to its slash or terminator.
        matched = true; // Allow the next iteration to consume a component separator.
    } // End this block.

    int count = scan_dir(backup_cluster); // Count the target directory chain; a root-only path leaves backup_cluster at zero.
    size = cluster_size * count; // Compute the bytes required for the complete directory cache.
    u8 *path_cache = new u8[size]; // Allocate a persistent buffer separate from the traversal scratch buffer.
    cluster = backup_cluster; // Restore the target directory's starting cluster.
    load_clusters(cluster, count, path_cache); // Load the entire target directory chain into the persistent cache.
    ret = increase_paths((u64)path_cache, (char *)path, count, backup_cluster); // Register the cache; path now points into the consumed input, not its original start.
    if (!ret.status) // Check whether registering the directory cache failed.
        delete[] path_cache; // Release the unregistered cache on failure.
    return ret; // Return the current status and registered index, if available.
} // End this block.


// Look up a previously cached directory by its stored path string.
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



// Reuse a cached directory/file when possible; otherwise open the directory
// once and search its entries. Handles pack path index high and file index low.
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

        // Compare fixed-width names after padding the requested component.
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




// Same directory-entry search as open_file_by_path, using an existing path index.
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

        // Compare fixed-width names after padding the requested component.
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

// Release loaded file data, compact the file table, and shrink its allocation.
// Handles contain array indices, so compaction changes later files' indices.
static inline void remove_file_entry(Paths_table *table, u32 file_index) {
    if (table->open_files == nullptr)
        return;
    
    cache_descriptor *filecache = table->open_files[file_index].cache;
    if (filecache != nullptr) {
        if (filecache->cache != nullptr)
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


// Unpack and validate both indices before removing the cached file record.
void BootFileSystem::close_file(u64 file_handle) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)(file_handle & 0xFFFFFFFF);

    if (path_index >= Path_count || table == nullptr)
        return;

    if (file_index >= table[path_index].file_count || table[path_index].open_files == nullptr)
        return;

    cache_descriptor *filecache = table->open_files[file_index].cache;
    if (filecache != nullptr && filecache->cache != nullptr) {
        write_clusters(table[path_index].open_files[file_index].starting_cluster, table[path_index].open_files[file_index].cluster_count, filecache->cache);
        if (!update_parent_dir(table[path_index].str, table[path_index].open_files[file_index].name, table[path_index].open_files[file_index].cluster_count * cluster_size)) {
            printf("FAT32: updating the file pointed by file handle 0x%lx failed\n", file_handle);
        }
    }
    remove_file_entry(&table[path_index], file_index);
    
}


// Close files in reverse order so removing one does not shift pending indices.
void BootFileSystem::close_path(u32 path_index) { // Close cached files, write directory data, and remove the path record.
    if (path_index >= Path_count || table == nullptr) // Reject an invalid path index or unavailable metadata table.
        return; // Leave without accessing an invalid path record.

        // close all files on path
   for (u32 i = table[path_index].file_count; i > 0; --i) { // Close files from the end so compaction does not shift pending file indices.
        u64 file_handle = ((u64)path_index << 32) | (u64)(i - 1); // Pack the parent path index high and the file index low into a handle.
        close_file(file_handle); // Release this file through the normal file-close routine.
   } // End this block.

   write_clusters(table[path_index].starting_cluster, table[path_index].cluster_count, (void *)Paths[path_index]); // Write the directory chain; 
   table[path_index] = {0}; // Clear metadata for the path being removed.
   kfree((void *)Paths[path_index]); // Release this directory's in-memory cache.
        // relocate paths entries
   for (u32 i = path_index; i < Path_count; i++) { // Shift later paths left; the final iteration currently reads index Path_count.
        table[i] = table[i+1]; // Move the next metadata record into the current slot.
        Paths[i] = Paths[i+1]; // Move its matching cache address into the same slot.
   } // End this block.

   u64 old_size = Path_count * sizeof(Paths_table); // Compute the current metadata allocation size in bytes.
   u64 new_size = old_size - sizeof(Paths_table); // Remove one metadata record from the requested allocation size.

   if (new_size == 0) // Free the array outright when removing its last entry.
        kfree(table); // Release the metadata array; the pointer is not reset here.
    else { // Shrink the array when entries remain.
        Paths_table *old_pointer = table; // Retain the original metadata allocation in case resizing fails.
        table = (Paths_table *)krealloc(table, old_size, new_size); // Request a smaller metadata array.
        if (table == nullptr) // Check whether metadata resizing failed.
            table = old_pointer; // Restore the original metadata pointer on failure.
    } // End this block.

    old_size = Path_count << 3; // Compute the cache-address array size: eight bytes per u64 address.
    new_size = old_size - 8; // Remove space for one cache address.

    if (new_size == 0) // Free the array outright when removing its last entry.
        kfree(Paths); // Release the cache-address array; the pointer is not reset here.
    else { // Shrink the array when entries remain.
        u64 *old_pointer = Paths; // Retain the original address array in case resizing fails.
        Paths = (u64 *)krealloc(Paths, old_size, new_size); // Request a smaller cache-address array.
        if (Paths == nullptr) // Check whether the address-array resize failed.
            Paths = old_pointer; // Restore the original cache-address pointer on failure.
    } // End this block.

    Path_count--; // Update the number of active cached paths after removal.

} // End this block.

// Grow the file data cache, with conditional on-disk chain extension. After each
// load, starting_cluster acts as a cursor to the next cluster still to be read.
void BootFileSystem::allocate_clusters(file_cache *file, u32 cluster_count, bool alloc) { // Load cluster_count additional clusters into the file cache, extending the disk chain in the EOC branches below.
    cache_descriptor *filecache = file->cache; // Get the descriptor for any file data already cached in memory.
    if (filecache == nullptr || file->cluster_count == 0) { // Create an initial cache when no descriptor or no cached clusters exist.
        filecache = new cache_descriptor[1]; // Allocate one descriptor to track the data buffer and append position.
        filecache->cluster_index = cluster_count; // Set the cache append position, measured in clusters, after this initial load.
        file->cluster_count = cluster_count; // Record how many clusters the initial cache is intended to contain.
        u64 size = cluster_count * cluster_size; // Convert the requested cluster count to a buffer size in bytes.
        filecache->cache = new u8[size]; // Allocate the initial data buffer.
        if (find_next_cluster(file->last_cluster) >= 0x0FFFFFF8) { // Check whether the cursor cluster points to the exact EOC marker used here.
            if (alloc)
                clusters_alloc(cluster_count, file->last_cluster); // Append cluster_count disk clusters after that cursor when the condition holds.
            else if (!alloc) {
               file->cluster_count = 1;
               cluster_count = 1;
               filecache->cluster_index = 1;
               size = cluster_size;
               delete[] filecache->cache;
               filecache->cache = new u8[size];
            }
        }
        file->last_cluster = load_clusters(file->last_cluster, cluster_count, filecache->cache); // Load the requested chain segment and advance starting_cluster to the FAT entry after it.
        file->cache = filecache; // Attach the initialized descriptor to the file record.
        return; // Finish the initial-cache branch.
    } // End initial-cache setup.


    if (find_next_cluster(file->last_cluster) >= 0x0FFFFFF8) {// Check whether the next-read cursor points to a cluster whose FAT entry is exactly EOC.
        if (alloc)
            clusters_alloc(cluster_count, file->last_cluster); // Extend the disk chain from that cursor by the requested number of clusters.
        else if (!alloc) 
            cluster_count = 1;
    }

    u64 cluster_off = filecache->cluster_index * cluster_size; // Convert the existing append position to a byte offset within the buffer.
    filecache->cluster_index += cluster_count; // Advance the planned append position by the number of clusters requested.
    u64 old_size = file->cluster_count * cluster_size; // Compute the currently recorded buffer size in bytes.
    file->cluster_count += cluster_count; // Increase the recorded cache length before reallocating the buffer.
    u64 new_size = file->cluster_count * cluster_size; // Compute the expanded buffer size in bytes.

    filecache->cache = (u8 *)krealloc(filecache->cache, old_size, new_size); // Resize the buffer; direct assignment does not retain its old pointer if resizing fails.
    file->cache = filecache; // Keep the file record attached to this descriptor.
    if (filecache->cache == nullptr) // Check whether buffer resizing failed.
        return; // Stop on failure; the counts above have already been increased.
     
    file->last_cluster = load_clusters(file->last_cluster, cluster_count, (void *)(filecache->cache + cluster_off)); // Load additional clusters into the appended buffer region and advance the next-read cursor.
} // Finish growing and filling the file cache.


void BootFileSystem::read_file(u64 file_handle, u64 offset, u32 size, void *buffer) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)file_handle;

    if (path_index >= Path_count || table == nullptr)
        return;

    if (file_index >= table[path_index].file_count || table[path_index].open_files == nullptr)
        return;

    file_cache *file = &table[path_index].open_files[file_index];

    // Round the end of the requested byte range up to whole clusters, then
    // load only the missing suffix before copying the requested bytes.
    u32 clusters = (offset + size + (cluster_size - 1)) / cluster_size;
    if (clusters > file->cluster_count) 
        allocate_clusters(file, (clusters - file->cluster_count), false);
    
    memcpy(buffer, (void *)(file->cache->cache+offset), size);
    
}

void BootFileSystem::write_file(u64 file_handle, u64 offset, u32 size, const void *buffer) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)file_handle;

    if (path_index >= Path_count || table == nullptr)
        return;

    if (file_index >= table[path_index].file_count || table[path_index].open_files == nullptr)
        return;

    file_cache *file = &table[path_index].open_files[file_index];

    // Round the end of the requested byte range up to whole clusters, then
    // load only the missing suffix before copying the requested bytes.
    u32 clusters = (offset + size + (cluster_size - 1)) / cluster_size;
    if (clusters > file->cluster_count) 
        allocate_clusters(file, (clusters - file->cluster_count), true);
    
    memcpy((void *)(file->cache->cache+offset), buffer, size);
    
}

// Terminate a chain at this cluster and detach the successor's reverse link.
// FAT writes below update the copy at fat_lba; other FAT copies are not mirrored.
void BootFileSystem::fat32_unlink(u32 cluster) { // End the chain at cluster; this assumes a non-null successor pointer.
    if (cluster == 0 || (cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF))
        return;


    u32 offset = cluster * 4; // Four bytes per FAT entry: compute this cluster's byte offset in the FAT.
    u32 sector = offset >> 9; // Divide by 512 to select the FAT sector containing the entry.
    u32 entry_offset = offset % 512; // Find the entry's byte offset within that sector.

    u8 buffer[512]; // Hold an entire sector so neighboring FAT entries can be preserved.

    drive->read_sectors(fat_lba + sector, 1, buffer); // Read the selected FAT sector before modifying its entry.

    u32 old_value = *(u32 *)(buffer + entry_offset); // Read all 32 bits, including the reserved upper four bits.

    u32 new_value = (old_value & 0xF0000000) | 0x0FFFFFFF; // Preserve reserved bits and set the low 28 bits to end of chain.

    if (cluster_map[cluster].next_cluster != nullptr) {
        cluster_map[cluster].next_cluster->previous_cluster = nullptr; // Remove the successor's reverse pointer to this cluster.
        cluster_map[cluster].next_cluster->previous_cluster_index = 0; // Clear the successor's recorded predecessor index.
        cluster_map[cluster].next_cluster_index = 0; // Record that this cluster no longer has a successor index.
        cluster_map[cluster].next_cluster = nullptr; // Clear this cluster's forward pointer without freeing either cluster.
    }

    cluster_map[cluster].fat_value = new_value; // Keep the in-memory FAT value consistent with the value being written.

    *(u32 *)(buffer + entry_offset) = new_value; // Replace only the selected four-byte entry in the sector buffer.

    drive->write_sectors(fat_lba + sector, 1, buffer); // Write the modified sector back to the FAT copy at fat_lba.
} // Finish terminating the chain at this cluster.


// Connect current to next, first detaching next from any recorded predecessor.
// Preserve the reserved upper nibble when replacing the on-disk FAT value.
void BootFileSystem::fat32_link(u32 current, u32 next) { // Set current's successor to next in memory and on disk.
    if (current == 0 || (current >= 0x0FFFFFF8 && current <= 0x0FFFFFFF))
        return;

    if (next == 0 || (next >= 0x0FFFFFF8 && next <= 0x0FFFFFFF))
        return;

    u32 offset = current * 4; // Locate current's four-byte entry relative to the start of the FAT.
    u32 sector = offset >> 9; // Divide by 512 to select the FAT sector containing the entry.
    u32 entry_offset = offset % 512; // Find the entry's byte offset within that sector.

    u8 buffer[512]; // Hold an entire sector so neighboring FAT entries can be preserved.

    drive->read_sectors(fat_lba + sector, 1, buffer); // Read the selected FAT sector before modifying its entry.

    u32 old_value = *(u32 *)(buffer + entry_offset); // Read all 32 bits, including the reserved upper four bits.

    u32 new_value = (old_value & 0xF0000000) | (next & 0x0FFFFFFF); // Preserve reserved bits and encode next in the low 28 bits.
    if (cluster_map[next].previous_cluster != nullptr) // Check whether next already has a recorded predecessor.
        fat32_unlink(cluster_map[next].previous_cluster_index); // Terminate that predecessor's chain before attaching next here.

    cluster_map[next].previous_cluster_index = current; // Record current as next's predecessor index.
    cluster_map[next].previous_cluster = &cluster_map[current]; // Record the matching reverse pointer to current.

    cluster_map[current].fat_value = new_value; // Cache the FAT entry that will be written for current.
    cluster_map[current].next_cluster_index = next; // Record current's successor index.
    cluster_map[current].next_cluster = &cluster_map[next]; // Record current's forward pointer to next.
    
    *(u32 *)(buffer + entry_offset) = new_value; // Replace only the selected four-byte entry in the sector buffer.

    drive->write_sectors(fat_lba + sector, 1, buffer); // Write the modified sector back to the FAT copy at fat_lba.
} // Finish linking the two clusters. 


// Mark one cluster free, detaching its predecessor. dont preserves the forward
// link temporarily so clusters_dealloc can continue walking the original chain.
void BootFileSystem::fat32_free(u32 cluster, bool dont) { // Free one FAT entry; dont controls whether its forward link is retained in memory.
    if (cluster == 0 || (cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF))
        return;


    u32 offset = cluster * 4; // Four bytes per FAT entry: compute this cluster's byte offset in the FAT.
    u32 sector = offset >> 9; // Divide by 512 to select the FAT sector containing the entry.
    u32 entry_offset = offset % 512; // Find the entry's byte offset within that sector.

    u8 buffer[512]; // Hold an entire sector so neighboring FAT entries can be preserved.

    drive->read_sectors(fat_lba + sector, 1, buffer); // Read the selected FAT sector before modifying its entry.

    u32 old_value = *(u32 *)(buffer + entry_offset); // Read all 32 bits, including the reserved upper four bits.

    u32 new_value = (old_value & 0xF0000000) | 0; // Preserve reserved bits and clear the low 28 bits to mark the cluster free.
    if (cluster_map[cluster].previous_cluster != nullptr) // Check whether a predecessor still points to this cluster.
        fat32_unlink(cluster_map[cluster].previous_cluster_index); // End the predecessor's chain and clear this cluster's reverse link.

    if (cluster_map[cluster].next_cluster != nullptr && !dont) // Detach the successor only when one exists and dont is false.
        fat32_unlink(cluster); // Clear the forward relationship; the final write below replaces EOC with free.


    cluster_map[cluster].fat_value = new_value; // Keep the in-memory FAT value consistent with the value being written.

    *(u32 *)(buffer + entry_offset) = new_value; // Replace only the selected four-byte entry in the sector buffer.

    drive->write_sectors(fat_lba + sector, 1, buffer); // Write the modified sector back to the FAT copy at fat_lba.
} // Finish freeing this cluster; its data sectors are not erased.


// Intended to find the directory terminator and return the space needed through
// the new entry. Ordinary live entries currently lack an advance in this loop.
static inline u64 next_entry_offset(u64 address, u64 size) {
    u8 *Path_cache = (u8 *)address; // Treat the cached directory address as a byte cursor.
    u64 inc = sizeof(FAT32_DIR); // Start with the size of one ordinary directory record.

    while (*Path_cache != 0x00) { // Stop at the first end-of-directory marker; cache bounds are not checked.
        if (*Path_cache == 0xE5) { // Deleted records are skipped rather than reused.
            Path_cache += inc; // Skip the combined record and fragment.
            continue;
        }


        FAT32_DIR *dir = (FAT32_DIR *)Path_cache; // Inspect the current record attributes.

        if (dir->attributes == LFN) {
            inc += sizeof(FAT32_LFN); // Include the adjacent long-name fragment used by this layout.
            Path_cache += inc; // Skip the combined record and fragment.
            inc = sizeof(FAT32_DIR); // Restore the ordinary record stride.
            continue;
        }

        Path_cache += inc;
    }

    // Return the end offset, not the insertion offset; callers subtract size.
    // This excludes space for the replacement 0x00 directory terminator.
    return (u64)(((u64)Path_cache + size) - address);
}



void BootFileSystem::mark_as_eoc(u32 cluster) {
    u32 offset = cluster * 4; // Four bytes per FAT entry: compute this cluster's byte offset in the FAT.
    u32 sector = offset >> 9; // Divide by 512 to select the FAT sector containing the entry.
    u32 entry_offset = offset % 512; // Find the entry's byte offset within that sector.

    u8 buffer[512]; // Hold an entire sector so neighboring FAT entries can be preserved.

    drive->read_sectors(fat_lba + sector, 1, buffer); // Read the selected FAT sector before modifying its entry.

    u32 old_value = *(u32 *)(buffer + entry_offset); // Read all 32 bits, including the reserved upper four bits.

    u32 new_value = (old_value & 0xF0000000) | 0x0FFFFFFF; // Preserve reserved bits and set the low 28 bits to end of chain.

    *(u32 *)(buffer + entry_offset) = new_value; // Replace only the selected four-byte entry in the sector buffer.

    drive->write_sectors(fat_lba + sector, 1, buffer); 
    cluster_map[cluster].next_cluster_index = 0; // Record that this cluster no longer has a successor index.
    cluster_map[cluster].next_cluster = nullptr;
    cluster_map[cluster].fat_value = new_value;
}


// Repeatedly find a free FAT entry and append it to the supplied chain tail.
// Finally mark the new tail as end of chain.
void BootFileSystem::clusters_alloc(u32 count, u32 cluster_link) { // Append up to count free clusters to the chain ending at cluster_link.
    for (u32 i = 0; i < count; i++) { // Find and attach one free cluster per iteration.
        u32 cluster = 1; // Start with a nonzero sentinel so an empty search does not imply success.
        u32 index = 0; // Initialize the candidate cluster index before searching.
        for (u32 j = 2; j < (clusters + 2); j++) { // Scan data-cluster records, skipping reserved cluster numbers 0 and 1.
            cluster = cluster_map[j].fat_value; // Read the candidate's cached FAT value; zero means free.
            index = j; // Remember the index corresponding to that FAT value.
            if (cluster == 0) // Check whether this candidate is available for allocation.
                break; // Stop searching at the first free cluster.
        } // Finish the free-cluster search.

        if (cluster != 0) // If no free entry was found, stop; clusters already appended remain allocated.
            return; // Return without rolling back earlier iterations or terminating the current tail.

            
        mark_as_eoc(index);
        fat32_link(cluster_link, index); // Link the current tail to the selected free cluster in memory and on disk.
        cluster_link = index; // Advance the tail so the next allocation is appended after this cluster.
    } // Finish this allocation iteration.

} // Finish extending the chain.

// Return a free data-cluster index, or zero when none is found. This only
// searches the cached FAT: the caller still needs to reserve the chosen entry.
u32 BootFileSystem::find_free_cluster(void) {
    u32 cluster = 0; // Hold the last inspected FAT value; an empty scan leaves it zero.
    u32 index = 0; // Zero is the failure result if the scan never runs.

     for (u32 j = 2; j < (clusters + 2); j++) { // Scan data-cluster records, skipping reserved cluster numbers 0 and 1.
            cluster = cluster_map[j].fat_value; // Read the candidate's cached FAT value; zero means free.
            index = j; // Remember the index corresponding to that FAT value.
            if (cluster == 0) // Check whether this candidate is available for allocation.
                break; // Stop searching at the first free cluster.
        } // Finish the free-cluster search.

        if (cluster != 0) // No free entry was found; this search leaves the FAT unchanged.
            return 0; // Report exhaustion; this search has not changed the FAT.
        
        return index; // Return the candidate without marking it allocated.
}

// Free count entries while preserving forward links until the final iteration.
void BootFileSystem::clusters_dealloc(u32 count, u32 cluster_link) { // Free count clusters beginning at cluster_link by following cached forward indices.
    for (u32 i = 0; i < count; i++) { // Process one cluster in the chain per iteration.
        u32 next = cluster_map[cluster_link].next_cluster_index;
        fat32_free(cluster_link, false);
        cluster_link = next;
    } // Finish this deallocation iteration.
    
} // Finish freeing the requested portion of the chain.


// Decode packed BCD from the RTC; assumes the RTC is operating in BCD mode.
static inline u8 rtc_to_num(u8 n) {
    n = n - ((n >> 4) * 6); // Subtract six per upper-nibble digit to convert BCD to binary.
    return n; // Return the decoded RTC field.
}

// Fill the cached record and optionally one adjacent long-name fragment.
// TODO: complete FAT32 LFN metadata/order and reserve the selected data cluster.
bool BootFileSystem::set_entry(const char *entry_name, bool lfn, FAT32_DIR *dir) {
    u64 size = sizeof(FAT32_DIR);
    if (lfn)
        size += sizeof(FAT32_LFN);

    memset(dir, 0, size);
    dir->attributes = lfn ? LFN : 0; // Currently puts the LFN attribute on the base record too.
    u8 min = rtc_to_num(rtc_read(0x02)); // Read minutes from the RTC and decode BCD.
    u8 hour = rtc_to_num(rtc_read(0x04)); // Read hours; assumes 24-hour RTC mode.
    u8 day = rtc_to_num(rtc_read(0x07)); // Read the day of the month.
    u8 month = rtc_to_num(rtc_read(0x08)); // Read the month number.
    u8 year = rtc_to_num(rtc_read(0x09)); // Read the two-digit year without a century adjustment.

    // FAT packs hours/minutes into time and a year offset from 1980 into date.
    // TODO: translate the RTC two-digit year to that epoch; seconds stay zero.
    dir->Time = (hour << 11) + (min << 5);
    dir->Date = (year << 9) + (month << 5) + (day & 0x1F);

    // This lookup does not allocate the cluster or handle the zero failure value.
    u32 cluster = find_free_cluster();
    if (cluster == 0) {
        printf("FAT32: no free space!\n");
        return false;
    }
    mark_as_eoc(cluster);
    dir->file_size = cluster_size; // Initialize the recorded size to one cluster.
    dir->cluster_low = (u16)cluster; // Store the low half of the starting cluster index.
    dir->cluster_high = (u16)(cluster >> 16); // Store the high half of the starting cluster index.

    if (lfn) {
        FAT32_LFN *ext = (FAT32_LFN *)((u64)dir + sizeof(FAT32_DIR)); // Use the fragment immediately after the base record.
        u16 name[13] = {0}; // Stage up to thirteen UTF-16 code units, initially zero.
        // TODO: the converter capacity is in bytes, and its result is unchecked.
        ascii_to_utf16le(entry_name, name, 13);
        memcpy(ext->first_part, name, 10); // Copy the first five UTF-16 code units.
        memcpy(ext->second_part, (void *)((u8 *)name + 10), 12); // Copy the next six UTF-16 code units.
        memcpy(ext->final_part, (void *)((u8 *)name + 22), 4); // Copy the final two UTF-16 code units.
        return true; // TODO: return false here because this function returns bool.
    }

    // Assumes the source supplies all 11 bytes of a padded short name.
    memcpy(dir->filename, entry_name, 11);
    return true;
}


// Find the child directory record in its parent cache, refresh its modification
// timestamp/size, then close the parent through the normal writeback path.
bool BootFileSystem::update_parent_dir(const char *path_name, const char *dir, u32 new_size) {
    Path_ret path = open_path(path_name); // Open/cache the parent directory before searching its entries.
    if (!path.status) // Stop if opening the parent failed.
        return false; 

    u8 *cache = (u8 *)Paths[path.index]; // Begin scanning the cached parent directory.
    u64 size = sizeof(FAT32_DIR); // Reserve one base directory record.
    

    while (*cache != 0) { // Scan until the directory terminator.
        if (*cache == 0xE5) { // Skip deleted records.
            cache += size;
            continue;
        }

        FAT32_DIR *dir32 = (FAT32_DIR *)((u64)cache); // Interpret this position as a directory record.
        u8 count = str_size(dir); // Measure the requested child component.
        u8 cmp_count = 11; // Compare all eleven bytes for short names.
        char *name = nullptr; // Own the decoded candidate name for this iteration.
        char *file = nullptr; // Own the padded requested name for comparison.


        if (dir32->attributes == LFN) {
            size += sizeof(FAT32_LFN); // Include a fragment; TODO: reset this stride for each candidate.
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)cache + sizeof(FAT32_DIR)); // Read the adjacent long-name fragment.
            u16 *utf_name = new u16[13]; // Allocate space for one fragment; collect_lfn requires a valid ordinal.
            collect_lfn(lfn, utf_name); // Reassemble the fragment into UTF-16 code units.
            name = new char[14]; // Allow thirteen ASCII characters plus a terminator.
            utf16le_to_ascii(utf_name, 13, name, 14); // Decode the candidate; the conversion result is not checked.
            delete[] utf_name; // Release the temporary UTF-16 buffer.
            cmp_count = 13; // Compare the full supported long-name width.
        } else {
            name = new char[11]; // Keep the short name as a fixed-width byte sequence.
            for (u8 i = 0; i < 11; i++) 
                name[i] = dir32->filename[i];
        }

        file = new char[cmp_count]; // Allocate the comparison buffer; count must fit this width.
        for (u8 i = 0; i < count; i++)
            file[i] = dir[i]; // Copy the requested name bytes.

        for (u8 i = count; i < cmp_count; i++)
            file[i] = ' '; // Pad the remaining comparison bytes with spaces.

        // Compare fixed-width names after padding the requested component.
        if (memcmp(file, name, cmp_count) == 0) {
            delete[] name; // Release the decoded candidate after finding a match.
            delete[] file; // Release the padded comparison buffer.
            u8 min = rtc_to_num(rtc_read(0x02));
            u8 hour = rtc_to_num(rtc_read(0x04));
            u8 day = rtc_to_num(rtc_read(0x07));
            u8 month = rtc_to_num(rtc_read(0x08));
            u8 year = rtc_to_num(rtc_read(0x09));

            dir32->last_modify_time = (hour << 11) + (min << 5);
            dir32->last_modify_date = (year << 9) + (month << 5) + (day & 0x1F);
            dir32->file_size = new_size; // Record the caller-supplied directory size in bytes.

            close_path(path.index); // Close the parent cache through its writeback path.
            return true; // Report that the matching parent entry was updated.
        }

        cache += size; // Advance to the next candidate; TODO: free name/file on a miss.

    }

    close_path(path.index); // Release the parent path even when the child was not found.
    return false; // Report that no matching child entry was found.
}

// Locate the separator before the final component, assuming a trailing slash.
// The input must contain that separator; this backward scan has no bounds check.
static inline int extract_place(const char *str) {
    int i = 0; // Start at the beginning of the cached path string.
    while (str[i] != '\0') 
        i++; // Find the terminating null byte.

    i -= 2; // Step back over the terminator and expected trailing slash.
    while (str[i] != '/') // Search backward for the parent/component separator.
        i--; // Move toward the parent prefix.

    return i; // Return the separator index, also the parent prefix length.
}

// Append a cached directory record, growing the directory chain if necessary,
// then update its record in the parent. Names of 12-13 characters use one LFN.
// TODO: finish validation, FAT32 record encoding, and directory writeback.
bool BootFileSystem::fat32_make_entry(u32 path_index, const char *entry_name) {
    u8 count = str_size(entry_name); // Measure the name; this currently narrows the length to eight bits.
    u64 size = sizeof(FAT32_DIR); // Reserve one base directory record.
    bool lfn = false;     // Default to the short-name path.

    if (count > 11 && count <= 13) {
        size += sizeof(FAT32_LFN); // Reserve one additional fragment for a 12-13 character name.
        lfn = true; // Select long-name encoding in set_entry.
    } else if (count > 13) {
        printf("FAT32: %s: name too big\n", entry_name);
        return false;
    }

    // TODO: Path_count is also outside the active array; reject equality too.
    if (path_index >= Path_count) {
        printf("FAT32: cannot find Path 0x%ux\n", path_index);
        return false;
    }

    Paths_table *Path = &table[path_index]; // Access the directory metadata corresponding to this cache.

    u32 cluster_count = Path->cluster_count; // Snapshot the cached cluster count; currently unused.

    u64 cache_addr = Paths[path_index]; // Retrieve the current directory buffer address.
    // offset points just past the new record(s); insertion starts at offset-size.
    u64 offset = next_entry_offset(cache_addr, size);
    int old_cluster_count = Path->cluster_count; // Keep the current allocation size for resizing.
    int new_cluster_count = (offset + (cluster_size - 1)) / cluster_size; // Round the required end offset up to whole clusters.
    // Grow the cache and disk chain together, then publish the new cache address.
    // TODO: include the terminator byte and roll back if either allocation fails.
    if (new_cluster_count > old_cluster_count) {
        int diff = new_cluster_count - old_cluster_count; // Compute how many clusters to append.
        cache_addr = resize_cache(cache_addr, old_cluster_count, new_cluster_count); // Resize the memory buffer; failure is encoded as address 1.
        u32 last_cluster = return_last_cluster(Path->starting_cluster, Path->cluster_count); // Locate the existing disk-chain tail.
        clusters_alloc(diff, last_cluster); // Append disk clusters; this runs before the cache failure check.
        if (cache_addr == 1) // Detect the resize helper failure sentinel.
            return false;

        Paths[path_index] = cache_addr; // Publish the possibly relocated directory buffer.
        Path->cluster_count = new_cluster_count; // Record the expanded cache length in clusters.
    }

    u8 *cache = (u8 *)cache_addr; // Use byte addressing within the final buffer.
    FAT32_DIR *new_dir = (FAT32_DIR *)(cache + (offset - size)); // Locate the start of the reserved record space.
    if (!set_entry(entry_name, lfn, new_dir)) { // Populate the cached record and optional long-name fragment.
        printf("FAT32: failed to set entry %s in path %s\n", entry_name, table[path_index].str);
        return false;
    }

    *(u8 *)(cache + offset) = 0x00; // no more entries

    // Split the cached path into its parent prefix and final directory name.
    // TODO: pass the updated directory size as the third argument below;
    // update_parent_dir currently has a different signature in the header.
    int new_count = extract_place(table[path_index].str); // Find the parent prefix length before the final component.
    char *par = new char[new_count+1]; // Allocate the parent prefix plus its null terminator.
    // The prefix length excludes the separator and leaves room for a null byte.
    par[new_count] = '\0'; // Terminate the prefix within the allocated buffer.
    
    memcpy(par, table[path_index].str, new_count); // Copy the parent prefix without its separating slash.
    if (!update_parent_dir(par, (const char *)((u64)table[path_index].str + (new_count+1)), (new_count*cluster_size))) {
        delete[] par; // Release the temporary parent string after an update failure.
        return false;
    }

    delete[] par; // Release the temporary parent string after a successful update.
    
    return true; // Report completion; the new directory cache is not closed here.
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
    if (index >= Path_count)
        return;

    u8 *cache = (u8 *)Paths[index];

    u64 size = sizeof(FAT32_DIR);
    while (*cache != 0x00) {
        if (*cache == 0xE5) {
            cache += size;
            continue;
        }

        FAT32_DIR *dir = (FAT32_DIR *)((u64)cache);
        char *name = nullptr;


        if (dir->attributes == LFN) {
            size += sizeof(FAT32_LFN);
            FAT32_LFN *lfn = (FAT32_LFN *)((u64)cache + sizeof(FAT32_DIR));
            u16 *utf_name = new u16[13];
            collect_lfn(lfn, utf_name);
            name = new char[14];
            utf16le_to_ascii(utf_name, 13, name, 14);
            delete[] utf_name;
            size = sizeof(FAT32_DIR);
        } else {
            name = new char[12];
            for (u8 i = 0; i < 11; i++) 
                name[i] = dir->filename[i];
            name[11] = '\0';
        }


        cache += size;

        char *new_name = dir->attributes == LFN ? nullptr : convert_fat32_to_str(name);
        printf("%s%s  ", new_name ? new_name : name,
               dir->attributes != LFN && (dir->attributes & 0x10) ? "/" : "");

        delete[] new_name;
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


char *BootFileSystem::return_str(u32 path_index) {
    char *str = table[path_index].str;
    return str;
}



u64 BootFileSystem::get_filesize(u64 file_handle) {
    u32 path_index = (u32)(file_handle >> 32);
    u32 file_index = (u32)(file_handle);

    if (path_index >= Path_count)
        return 0;  // 0 size means failure here

    if (file_index >= table[path_index].file_count)
        return 0;

    int cluster_count = table[path_index].open_files[file_index].cluster_count;

    return (u64)(cluster_count * cluster_size);
}