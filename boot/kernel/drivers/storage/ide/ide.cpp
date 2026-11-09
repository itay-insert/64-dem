#include "uint_definitions.h"
#include "drivers/storage/ide/ata.h"
#include "drivers/storage/ide/ide.hpp"
#include "drivers/pci/pci.h"
#include "drivers/display/vga.h"

extern u16 Primary;
extern u16 Secondary;

extern u16 PB;
extern u16 SB;

bool ata_discovered = false;
bool ata_end = false;

#define P_CTL (PB + 2)
#define S_CTL (SB + 2)

ata_channel_t primary_channel = {};
ata_channel_t secondary_channel = {};

void ata_find_callback(const PCI_ret *device, void *context) {

    if (ata_discovered || device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != 0x01 || device->common.subclass != 0x01)
        return;

    u8 prog_if = device->common.prog_if;

    pci_address_t pci_address = {
        .segment = device->Segment,
        .bus = device->Bus,
        .device = device->Device,
        .function = device->Function
    };

    pci_bar_t bar0; 
    pci_read_bar(pci_address, 0, 0, &bar0);
    pci_bar_t bar1; 
    pci_read_bar(pci_address, 1, 0, &bar1);
    pci_bar_t bar2; 
    pci_read_bar(pci_address, 2, 0, &bar2);
    pci_bar_t bar3; 
    pci_read_bar(pci_address, 3, 0, &bar3);

    // if ata is in pci native mode, use i/o bases from bars otherwise use legacy i/o bases

    Primary = (prog_if & 0x01) ? (u16)bar0.address : 0x1F0;
    PB = (prog_if & 0x01) ? (u16)bar1.address : 0x3F4;

    Secondary = (prog_if & 0x04) ? (u16)bar2.address : 0x170;
    SB = (prog_if & 0x04) ? (u16)bar3.address : 0x374;

    primary_channel.io_base = Primary;
    secondary_channel.io_base = Secondary;

    primary_channel.control_port = P_CTL;
    secondary_channel.control_port = S_CTL;

    ata_discovered = true;
}

bool ata_bitmap[4] = {false}; // initialize all the array to false

static ata_drive Search_bitmap(void) {
    ata_drive drive = {};

    for (u8 i = 0; i < 4; i++) {
        if (!ata_bitmap[i]) {
            drive.disk.channel = i < 2 ? &primary_channel : &secondary_channel;
            drive.disk.slave = (i & 1);
            drive.found = true;
            ata_bitmap[i] = true;
            return drive;
        }
    }

    drive.found = false;
    printf("ATA: no free drive found\n");
    return drive;
}

void ATA_drive::init() {
    if (!ata_discovered) {
        if (pci_enumerate(ata_find_callback, nullptr) != PCI_STATUS_SUCCESS) {
            printf("ATA: no ide device found on pci\n");
            ata_end = true;
            return;
        } 
    }

    drive = Search_bitmap();
    if (!drive.found) 
        ata_end = true;
    else {
        drive.block_count = 512;
        ata_probe_t probe = ata_probe_device(&drive.disk);
        if (probe != ATA_DEVICE_ATA && probe != ATA_DEVICE_ATAPI) {
            printf("ATA: unknown device or probe error\n");
            drive.found = false;
            return;
        }
    }

}


void ATA_drive::read_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (buffer == nullptr)
        return;
    
    u32 iterations = sector_count / 0xFF;
    u8 remainder = sector_count % 0xFF;
    
    if (remainder != 0) {
        ata_read(&drive.disk, lba, buffer, remainder);
        buffer = (void *)((u8 *)buffer + (remainder << 9)); // << 9 == * 512
        lba += remainder;
    }

    for (u32 i = 0; i < iterations; i++) {
        ata_read(&drive.disk, lba, buffer, 0xFF);
        buffer = (void *)((u8 *)buffer + (0xFF << 9));
        lba += 0xFF;
    }

}


void ATA_drive::write_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (buffer == nullptr)
        return;

    u32 iterations = sector_count / 0xFF;
    u8 remainder = sector_count % 0xFF;

    if (remainder != 0) {
        ata_write(&drive.disk, lba, buffer, remainder);
        buffer = (void *)((u8 *)buffer + (remainder << 9)); // << 9 == * 512
        lba += remainder;
    }

    for (u32 i = 0; i < iterations; i++) {
        ata_write(&drive.disk, lba, buffer, 0xFF);
        buffer = (void *)((u8 *)buffer + (0xFF << 9));
        lba += 0xFF;
    }
}