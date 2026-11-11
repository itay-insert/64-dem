#include "const_new.hpp"
#include "drivers/storage/storage.hpp"
#include "drivers/storage/ide/ide.hpp"
#include "x86-64/memory/kmalloc.h"
#include "drivers/storage/ums.hpp"
#include "drivers/storage/nvme.hpp"
#include "drivers/storage/ide/sata.hpp"


extern bool controller_init(TimerSource &timer);
extern u8 port_count;
extern u8 current_port;
extern bool ata_end;

extern u16 current_drive;
extern u16 usb_drive_count;
extern void ums_discover();
extern void free_drives();
extern u64 timeradd;

bool storage::increase_drives(u64 ClassSize) {
    if (!ClassSize || ClassSize > ~0ull - drives_size)
        return false;
    if (drives == nullptr || drives_size == 0) {
        drives = (disk_drive *)kmalloc(ClassSize);
        if (!drives) return false;
        drives_size = ClassSize;
        return true;
    }

    disk_drive *grown = (disk_drive *)krealloc(drives, drives_size, drives_size + ClassSize);
    if (!grown) return false;
    drives = grown;
    drives_size += ClassSize;
    return true;
}


bool storage::increase_offsets(u8 count) {
    if (offsets == nullptr || count == 0) {
        offsets = (u64 *)kmalloc(sizeof(u64));
        return offsets != nullptr;
    }

    u64 *grown = (u64 *)krealloc(offsets, (u64)count << 3, ((u64)count + 1) << 3);
    if (!grown) return false;
    offsets = grown;
    return true;
}


disk_drive *storage::find_boot_drive() {
    u8 sector[512] = {0};

    for (u8 i = 0; i < drive_count; i++) {
        u64 offset = offsets[i];
        disk_drive *drive = (disk_drive *)((u8 *)drives + offset);
        if (drive->initialized) {
            drive->read_sectors(0, 1, sector);
            u8 *sign = sector + 436;
            if (memcmp(sign, "64DM", 4) == 0) {
                return drive;
            }
        }
    }

    return nullptr;
}

storage::storage() {
    ata_end = false; // initialize ata status

    offsets = nullptr;

    drives = nullptr;
    drives_size = 0; // initialize drives as null
    drive_count = 0;

    current_drive = 0;
    usb_drive_count = 0;

    while (!ata_end && drive_count < 255) {
        u64 old_size = drives_size;
        if (!increase_drives(sizeof(ATA_drive))) break;
        void *slot = reinterpret_cast<u8 *>(drives) + old_size;
        ATA_drive *drive = new (slot) ATA_drive{};
        drive->sector_size = 512;
        drive->init();
        if (!drive->is_found())
            continue;
        if (!increase_offsets(drive_count)) break;
        offsets[drive_count] = (u64)drive - (u64)drives;
        drive->disk_index = drive_count;
        drive_count++;
    }

    ums_discover(); // discover USB devices;

    while (current_drive < usb_drive_count && drive_count < 255) {
        u64 old_size = drives_size;
        if (!increase_drives(sizeof(UMS_drive))) break;
        void *slot = reinterpret_cast<u8 *>(drives) + old_size;
        UMS_drive *drive = new (slot) UMS_drive{};
        if (!increase_offsets(drive_count)) break;
        offsets[drive_count] = (u64)drive - (u64)drives;
        drive->disk_index = drive_count;
        drive->init();
        drive_count++;
    }

    Timer *timer = (Timer *)timeradd;
    if (timer && timer->Src) {
        nvme_discover(*timer->Src);
        u32 count = nvme_namespace_count();
        for (u32 i = 0; i < count && drive_count < 255; ++i) {
            u64 old_size = drives_size;
            if (!increase_drives(sizeof(NVMe_drive))) break;
            void *slot = reinterpret_cast<u8 *>(drives) + old_size;
            NVMe_drive *drive = new (slot) NVMe_drive(i);
            if (!increase_offsets(drive_count)) break;
            offsets[drive_count] = (u64)drive - (u64)drives;
            drive->disk_index = drive_count;
            drive->init();
            drive_count++;
        }
    }


    if (controller_init(*timer->Src)) {
        while (current_port < port_count) {
            u64 old_size = drives_size;
            if (!increase_drives(sizeof(SATA_drive))) break;
            void *slot = reinterpret_cast<u8 *>(drives) + old_size;
            SATA_drive *drive = new (slot) SATA_drive{};
            if (!increase_offsets(drive_count)) break;
            offsets[drive_count] = (u64)drive - (u64)drives;
            drive->disk_index = drive_count;
            drive->init();
            drive_count++;
        }
    }
}
