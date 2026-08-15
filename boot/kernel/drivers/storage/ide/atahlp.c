#include "x86-64/ports.h"
#include "drivers/storage/ide/ata.h"

void ata_400ns_delay(u16 control_port) {
    inb(control_port);
    inb(control_port);
    inb(control_port);
    inb(control_port);
}


bool ata_wait_not_busy(u16 io_base) {
    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        u8 status = inb(io_base + ATA_REG_STATUS);

        if (status == 0 || status == 0xFF)
            return false;

        if (!(status & ATA_SR_BSY))
            return true;
    }

    return false;
}



bool ata_poll_drq(ata_drive_t *drive) {
    if (!drive || !drive->channel)
        return false;

    u16 status_port = drive->channel->io_base + ATA_REG_STATUS;

    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        u8 status = inb(status_port);

        if (status == 0 || status == 0xFF) 
            return false;

        if (!(status & ATA_SR_BSY)) {
            if (status & (ATA_SR_ERR | ATA_SR_DF))
                return false;
            
            if (status & ATA_SR_DRQ)
                return true;
        }
    }

    return false;
}
