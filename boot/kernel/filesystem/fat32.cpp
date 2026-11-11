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



u32 BootFileSystem::Cluster_To_LBA(u32 cluster) {
    u32 lba = first_data_lba + ((cluster - 2) * SectorsPerCluster); // cluster size would practically almost always be powers of two
    return lba;
}


u32 BootFileSystem::LBA_to_Cluster(u32 lba) {
    u32 cluster = 2 + (lba - first_data_lba) / SectorsPerCluster;
    return cluster; 
}



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

    root_lba = Cluster_To_LBA(ebr->RootCluster);
    delete[] sector;
    printf("boot drive 0x%b:         \n", drive->disk_index);
    printf("Filesystem: FAT32, Capacity: %lu MB, Free Space: %lu MB, Used Space: %lu MB\n", (SizeInBytes / 0x100000), (SizeFree / 0x100000), (SizeUsed / 0x100000));

}




bool BootFileSystem::increase_paths(u64 address, char *path) {
    if (Paths == nullptr || Path_count == 0) {
        Paths = new u64[1];
        Paths[0] = address;
        table = new Paths_table[1];
        table[0].index = 0;
        table[0].str = path;
        Path_count = 1;
        return true;
    }

    u64 paths_size = 8 * Path_count;
    u64 new_size = paths_size + 8;
    Paths = (u64 *)krealloc(Paths, paths_size, new_size);

    if (Paths == nullptr)
        return false;

    u64 table_size = sizeof(Paths_table) * Path_count;
    new_size = table_size + sizeof(Paths_table);
    table = (Paths_table *)krealloc(table, table_size, new_size);

    if (table == nullptr)
        return false;

    Path_count++;
    return true;
}



u8 dir_place[4096] = {0};

static inline u8 count_path(const char *path) {
    u8 count = 0;
    while (*path != '/') {
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


bool BootFileSystem::open_path(const char *path) {
    u32 sector_count = 4096 >> 9;
    u32 lba = root_lba;
    drive->read_sectors(lba, sector_count, 4096, dir_place);

    u64 size = sizeof(FAT32_DIR);
    u8 *dir_data = dir_place;
    bool started = true;


    while (path[1] != '\0') {
        start:

        if (started) {
            started = false;
            goto skip_add;
        }

        if (((u64)dir_data + size) >= ((u64)dir_place + 4096)) {
            lba += sector_count;
            drive->read_sectors(lba, sector_count, 4096, dir_place);
            dir_data = dir_place;
            size = sizeof(FAT32_DIR);
        } else {
            dir_data += size;
            size = sizeof(FAT32_DIR);
        }

        skip_add:
        if (*dir_data == 0xE5)  // deleted entry
            goto start;
        
        if (*dir_data == 0x00) // no more entries
            return false;


        if (*path != '/') 
            return false;
        

        path++;
        u8 count = count_path(path);
        if (count == 0) 
            return false;
        
        FAT32_DIR *dir = (FAT32_DIR *)((u64)dir_data);

        if (dir->attributes == LFN) {
            size += sizeof(FAT32_LFN);
        }


    }
}
