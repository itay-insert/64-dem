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

storage::storage() {
    ata_end = false; // initialize ata status

    drives = nullptr;
    drives_size = 0; // initialize drives as null
    drive_count = 0;

    current_drive = 0;
    usb_drive_count = 0;

    while (!ata_end) {
        increase_drives(sizeof(ATA_drive));
        ATA_drive *drive = reinterpret_cast<ATA_drive *>(&drives[drive_count]);
        drive->init();
        drive_count++;
    }

    ums_discover(); // discover USB devices;

    while (current_drive < usb_drive_count) {
        increase_drives(sizeof(UMS_drive));
        UMS_drive *drive = reinterpret_cast<UMS_drive *>(&drives[drive_count]);
        drive->init();
        drive_count++;
    }
    


}



