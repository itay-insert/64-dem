#include "drivers/storage/ide/ata.h"
#include "x86-64/ports.h"
#include <stddef.h>

bool ata_reset_channel(u16 control_port) {
    outb(control_port, ATA_CTL_NIEN | ATA_CTL_SRST);
    ata_400ns_delay(control_port);

    
    outb(control_port, ATA_CTL_NIEN);
    ata_400ns_delay(control_port);

    for (u32 timeout = 0; timeout < 1000000; ++timeout) {
        u8 status = inb(control_port);

        if (status == 0 || status == 0xFF)
              return false;

        if (!(status & ATA_SR_BSY))
              return true;
    }

    return false;

}


void ata_select_drive(u16 io_base, 
u16 control_port, bool slave) {
    outb(io_base + ATA_REG_DRIVE_HEAD, slave ? 
    0xB0 : 0xA0);

    ata_400ns_delay(control_port);
}




static void ata_copy_string(char *dst, u32 dst_size, const u16 *identify, 
u32 first_word, u32 word_count) {
    u32 length = word_count << 1;

    if (length >= dst_size)
        length = dst_size - 1;

    for (u32 i = 0; i < length; i++) {
        u16 word = identify[first_word + i / 2];

        if ((i & 1) == 0) 
            dst[i] = (char)(word >> 8);
        else 
            dst[i] = (char)(word & 0xFF);
    }

    while (length > 0 && 
                (dst[length - 1] == ' ' || dst[length - 1] == '\0')) {
        length--;                    
    }

    dst[length] = '\0';
}



ata_probe_t ata_probe_device(ata_drive_t *drive) {
    u16 words[256];
    ata_probe_t result = ATA_PROBE_PROTOCOL_ERROR;
    ata_device_type_t device_type = ATA_DEVICE_NONE;

    if (drive == NULL || drive->channel == NULL)
        return ATA_PROBE_PROTOCOL_ERROR;

    drive->type = ATA_DEVICE_NONE;
    drive->supports_lba28 = false;
    drive->supports_lba48 = false;
    drive->sector_count = 0;
    drive->model[0] = '\0';
    drive->serial[0] = '\0';
    drive->firmware[0] = '\0';
    drive->ata_status = SUCCESS;

    ata_channel_t *channel = drive->channel;
    u16 io = channel->io_base;

    spin_lock(&channel->lock);

    ata_select_drive(io, channel->control_port, drive->slave);
    if (!ata_wait_not_busy(io)) {
        result = ATA_PROBE_TIMEOUT;
        goto out;
    }

    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECTOR_COUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    u8 status = 0;
    bool timed_out = true;
    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        status = inb(io + ATA_REG_STATUS);

        if (status == 0 || status == 0xFF) {
            result = ATA_PROBE_NONE;
            goto out;
        }

        if (status & ATA_SR_BSY)
            continue;

        timed_out = false;
        break;
    }

    if (timed_out) {
        result = ATA_PROBE_TIMEOUT;
        goto out;
    }

    if (status & ATA_SR_DF) {
        result = ATA_PROBE_DEVICE_FAULT;
        goto out;
    }

    if (status & ATA_SR_ERR) {
        u8 signature_mid = inb(io + ATA_REG_LBA_MID);
        u8 signature_high = inb(io + ATA_REG_LBA_HIGH);

        if (!((signature_mid == 0x14 && signature_high == 0xEB) ||
              (signature_mid == 0x69 && signature_high == 0x96))) {
            result = ATA_PROBE_PROTOCOL_ERROR;
            goto out;
        }

        outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

        timed_out = true;
        for (u32 timeout = 0; timeout < 1000000; timeout++) {
            status = inb(io + ATA_REG_STATUS);

            if (status == 0 || status == 0xFF) {
                result = ATA_PROBE_NONE;
                goto out;
            }

            if (status & ATA_SR_BSY)
                continue;

            timed_out = false;
            break;
        }

        if (timed_out) {
            result = ATA_PROBE_TIMEOUT;
            goto out;
        }

        if (status & ATA_SR_DF) {
            result = ATA_PROBE_DEVICE_FAULT;
            goto out;
        }

        if ((status & ATA_SR_ERR) || !(status & ATA_SR_DRQ)) {
            result = ATA_PROBE_PROTOCOL_ERROR;
            goto out;
        }

        device_type = ATA_DEVICE_ATAPI;
        result = ATA_PROBE_ATAPI;
    } else {
        if (!(status & ATA_SR_DRQ)) {
            result = ATA_PROBE_PROTOCOL_ERROR;
            goto out;
        }

        device_type = ATA_DEVICE_ATA;
        result = ATA_PROBE_ATA;
    }

    for (u32 i = 0; i < 256; i++)
        words[i] = inw(io + ATA_REG_DATA);

    if (((words[0] & 0x8000) != 0) !=
        (device_type == ATA_DEVICE_ATAPI)) {
        result = ATA_PROBE_PROTOCOL_ERROR;
        goto out;
    }

    drive->type = device_type;
    ata_copy_string(drive->serial, sizeof(drive->serial), words, 10, 10);
    ata_copy_string(drive->firmware, sizeof(drive->firmware), words, 23, 4);
    ata_copy_string(drive->model, sizeof(drive->model), words, 27, 20);

    if (device_type == ATA_DEVICE_ATA) {
        drive->supports_lba28 = (words[49] & (1u << 9)) != 0;

        bool word83_valid = (words[83] & 0xC000) == 0x4000;
        drive->supports_lba48 =
            word83_valid && ((words[83] & (1u << 10)) != 0);

        if (drive->supports_lba48) {
            drive->sector_count =
                (u64)words[100] |
                ((u64)words[101] << 16) |
                ((u64)words[102] << 32) |
                ((u64)words[103] << 48);
        }

        if (drive->sector_count == 0 && drive->supports_lba28) {
            drive->sector_count =
                (u64)words[60] | ((u64)words[61] << 16);
        }
    }

out:
    spin_unlock(&channel->lock);
    return result;
}
