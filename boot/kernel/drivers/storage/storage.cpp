#include "const_new.hpp"
#include "drivers/storage/storage.hpp"
#include "drivers/storage/ide/ide.hpp"
#include "x86-64/memory/kmalloc.h"
#include "drivers/storage/ums.hpp"


extern bool ata_end;

extern u16 current_drive;
extern u16 usb_drive_count;
extern void ums_discover();
extern void free_drives();

void storage::increase_drives(u64 ClassSize) {
    if (drives == nullptr || drives_size == 0) {
        drives = (disk_drive *)kmalloc(ClassSize);
        drives_size += ClassSize;
        return;
    }

    drives = (disk_drive *)krealloc(drives, drives_size, (drives_size+ClassSize));
    drives_size += ClassSize;
}


void storage::increase_offsets(u8 count) {
    if (offsets == nullptr || count == 0) {
        offsets = new u64[1];
        return;
    }

    offsets = (u64 *)krealloc(offsets, count<<3, (count+1)<<3);
    return;
}


storage::storage() {
    ata_end = false; // initialize ata status

    offsets = nullptr;

    drives = nullptr;
    drives_size = 0; // initialize drives as null
    drive_count = 0;

    current_drive = 0;
    usb_drive_count = 0;

    while (!ata_end) {
        u64 old_size = drives_size;
        increase_drives(sizeof(ATA_drive));
        void *slot = reinterpret_cast<u8 *>(drives) + old_size;
        ATA_drive *drive = new (slot) ATA_drive{};
        increase_offsets(drive_count);
        offsets[drive_count] = (u64)drive - (u64)drives;
        drive->init();
        drive_count++;
    }

    ums_discover(); // discover USB devices;

    while (current_drive < usb_drive_count) {
        u64 old_size = drives_size;
        increase_drives(sizeof(UMS_drive));
        void *slot = reinterpret_cast<u8 *>(drives) + old_size;
        UMS_drive *drive = new (slot) UMS_drive{};
        increase_offsets(drive_count);
        offsets[drive_count] = (u64)drive - (u64)drives;
        drive->init();
        drive_count++;
    }
    


}



