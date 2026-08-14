#include "ata.h"
#include "ports.h"


ata_addressing_mode_t ata_configure_lba(ata_drive_t *drive, u64 lba, u8 sector_count) {
    if (!drive || drive->type != ATA_DEVICE_ATA)
        return ATA_ADDRESSING_INVALID;

    if (lba >= drive->sector_count || lba >= (1ULL << 48))
        return ATA_ADDRESSING_INVALID;

    u16 io = drive->io_base;

    if (!ata_wait_not_busy(io))
        return ATA_ADDRESSING_INVALID;

    if (drive->supports_lba28 && lba < (1ULL << 28)) {
        outb(io + ATA_REG_DRIVE_HEAD,
             0xE0 | (drive->slave ? 0x10 : 0) |
             (u8)((lba >> 24) & 0x0F));
        ata_400ns_delay(drive->control_port);

        outb(io + ATA_REG_FEATURES, 0);
        outb(io + ATA_REG_SECTOR_COUNT, sector_count);
        outb(io + ATA_REG_LBA_LOW, (u8)lba);
        outb(io + ATA_REG_LBA_MID, (u8)(lba >> 8));
        outb(io + ATA_REG_LBA_HIGH, (u8)(lba >> 16));

        return ATA_ADDRESSING_LBA28;
    }

    if (!drive->supports_lba48)
        return ATA_ADDRESSING_INVALID;

    outb(io + ATA_REG_DRIVE_HEAD,
         0xE0 | (drive->slave ? 0x10 : 0));
    ata_400ns_delay(drive->control_port);

    /* LBA48 task files must receive the high-order bytes first. */
    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECTOR_COUNT, 0);
    outb(io + ATA_REG_LBA_LOW, (u8)(lba >> 24));
    outb(io + ATA_REG_LBA_MID, (u8)(lba >> 32));
    outb(io + ATA_REG_LBA_HIGH, (u8)(lba >> 40));

    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECTOR_COUNT, sector_count);
    outb(io + ATA_REG_LBA_LOW, (u8)lba);
    outb(io + ATA_REG_LBA_MID, (u8)(lba >> 8));
    outb(io + ATA_REG_LBA_HIGH, (u8)(lba >> 16));

    return ATA_ADDRESSING_LBA48;
}


void ata_write(ata_drive_t *drive, u64 lba, void *buffer, u8 count) {
    if (!(count > 0) || !(lba + count <= drive->sector_count)) {
        drive->ata_status = ERR_INVALID_COUNT;
        return;
    }
    
}