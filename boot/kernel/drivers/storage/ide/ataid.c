#include "drivers/storage/ide/ata.h"
#include "x86-64/ports.h"

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

    // initialize the drive
    drive->type = ATA_DEVICE_NONE;
    drive->supports_lba28 = false;
    drive->supports_lba48 = false;
    drive->sector_count = 0;
    drive->model[0] = '\0';
    drive->serial[0] = '\0';
    drive->firmware[0] = '\0';
    drive->ata_status = SUCCESS;

    ata_channel_t *channel = drive->channel; // lock channel
    u16 io = channel->io_base;
    u16 CTP = channel->control_port;
    bool slave = drive->slave;

    spin_lock(&channel->lock);

    ata_select_drive(io, CTP, slave);
    bool ret = ata_wait_not_busy(io);

    if (ret == false) return ATA_PROBE_TIMEOUT;



    outb(io + ATA_REG_FEATURES, 0);    // clearing the task-file registers
    outb(io + ATA_REG_SECTOR_COUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY); // ata_identify command

    u8 status = 0;
    bool timed_out = true;
    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        status = inb(io + ATA_REG_STATUS);

        if (status == 0 || status == 0xFF) {
            spin_unlock(&channel->lock);
            return ATA_PROBE_NONE;
        }

        if (status & ATA_SR_BSY)
            continue;
            
        timed_out = false;
        break;
    }

    if (timed_out) {
        spin_unlock(&channel->lock);
        return ATA_PROBE_TIMEOUT;
    }

    if (status & ATA_SR_DF) {
        spin_unlock(&channel->lock);
        return ATA_PROBE_DEVICE_FAULT;
    }

    if (!(status & ATA_SR_ERR) && (status & ATA_SR_DRQ)) {
        for (int i = 0; i < 256; i++) 
            words[i] = inw(io + ATA_REG_DATA);
    }
}
