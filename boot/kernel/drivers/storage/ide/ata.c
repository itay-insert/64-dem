#include "drivers/storage/ide/ata.h"
#include "x86-64/ports.h"
#include <stddef.h>

ata_channel_t primary_channel = {
    .io_base = Primary,
    .control_port = P_CTL,
    .lock = { .locked = 0 }
};

ata_channel_t secondary_channel = {
    .io_base = Secondary,
    .control_port = S_CTL,
    .lock = { .locked = 0 }
};

ata_addressing_mode_t ata_configure_lba(ata_drive_t *drive, u64 lba, u8 sector_count) {
    if (!drive || !drive->channel || drive->type != ATA_DEVICE_ATA)
        return ATA_ADDRESSING_INVALID;

    if (lba >= drive->sector_count || lba >= (1ULL << 48))
        return ATA_ADDRESSING_INVALID;

    u16 io = drive->channel->io_base;

    if (!ata_wait_not_busy(io))
        return ATA_ADDRESSING_INVALID;

    if (drive->supports_lba28 &&
        lba < (1ULL << 28) &&
        sector_count <= (1ULL << 28) - lba) {
        outb(io + ATA_REG_DRIVE_HEAD,
             0xE0 | (drive->slave ? 0x10 : 0) |
             (u8)((lba >> 24) & 0x0F));
        ata_400ns_delay(drive->channel->control_port);

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
    ata_400ns_delay(drive->channel->control_port);

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
    if (!drive)
        return;

    if (!drive->channel || drive->type != ATA_DEVICE_ATA) {
        drive->ata_status = ERR_INVALID_TYPE;
        return;
    }

    if (!buffer) {
        drive->ata_status = ERR_INVALID_BUFFER;
        return;
    }

    if (count == 0 || lba >= drive->sector_count ||
        count > drive->sector_count - lba) {
        drive->ata_status = ERR_INVALID_COUNT;
        return;
    }

    ata_channel_t *channel = drive->channel;
    u16 io = channel->io_base;
    const u8 *bytes = (const u8 *)buffer;

    spin_lock(&channel->lock);

    ata_addressing_mode_t mode = ata_configure_lba(drive, lba, count);
    if (mode == ATA_ADDRESSING_INVALID)
        goto io_error;

    outb(io + ATA_REG_COMMAND,
         mode == ATA_ADDRESSING_LBA48
             ? ATA_CMD_WRITE_EXT
             : ATA_CMD_WRITE);

    for (u32 sector = 0; sector < count; sector++) {
        if (!ata_poll_drq(drive))
            goto io_error;

        for (u32 word = 0; word < 256; word++) {
            u16 value = (u16)bytes[0] | ((u16)bytes[1] << 8);
            outw(io + ATA_REG_DATA, value);
            bytes += 2;
        }

        ata_400ns_delay(channel->control_port);
    }

    if (!ata_wait_not_busy(io))
        goto io_error;

    u8 status = inb(io + ATA_REG_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF))
        goto io_error;

    outb(io + ATA_REG_COMMAND,
         mode == ATA_ADDRESSING_LBA48
             ? ATA_CMD_FLUSH_EXT
             : ATA_CMD_FLUSH);

    if (!ata_wait_not_busy(io))
        goto io_error;

    status = inb(io + ATA_REG_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF))
        goto io_error;

    drive->ata_status = SUCCESS;
    spin_unlock(&channel->lock);
    return;

io_error:
    drive->ata_status = ERR_IO;
    spin_unlock(&channel->lock);
}



void ata_read(ata_drive_t *drive, u64 lba, void *buffer, u8 count) {
    if (!drive)
        return;

    if (!drive->channel || drive->type != ATA_DEVICE_ATA) {
        drive->ata_status = ERR_INVALID_TYPE;
        return;
    }

    if (!buffer) {
        drive->ata_status = ERR_INVALID_BUFFER;
        return;
    }

    if (count == 0 || lba >= drive->sector_count ||
        count > drive->sector_count - lba) {
        drive->ata_status = ERR_INVALID_COUNT;
        return;
    }

    ata_channel_t *channel = drive->channel;
    u16 io = channel->io_base;
    u8 *bytes = (u8 *)buffer;

    spin_lock(&channel->lock);

    ata_addressing_mode_t mode = ata_configure_lba(drive, lba, count);
    if (mode == ATA_ADDRESSING_INVALID)
        goto io_error;

    outb(io + ATA_REG_COMMAND,
         mode == ATA_ADDRESSING_LBA48
             ? ATA_CMD_READ_EXT
             : ATA_CMD_READ);

    for (u32 sector = 0; sector < count; sector++) {
        if (!ata_poll_drq(drive))
            goto io_error;

        for (u32 word = 0; word < 256; word++) {
            u16 value = inw(io + ATA_REG_DATA);
            bytes[0] = (u8)value;
            bytes[1] = (u8)(value >> 8);
            bytes += 2;
        }

        ata_400ns_delay(channel->control_port);
    }

    if (!ata_wait_not_busy(io))
        goto io_error;

    u8 status = inb(io + ATA_REG_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF))
        goto io_error;

    drive->ata_status = SUCCESS;
    spin_unlock(&channel->lock);
    return;

io_error:
    drive->ata_status = ERR_IO;
    spin_unlock(&channel->lock);
}
