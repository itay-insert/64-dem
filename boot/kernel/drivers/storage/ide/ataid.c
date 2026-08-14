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



bool ata_identify(ata_drive_t *drive, 
u16 identify[256]) {
    drive->type = ATA_DEVICE_NONE;
    drive->supports_lba28 = false;
    drive->supports_lba48 = false;
    drive->sector_count = 0;
    drive->model[0] = '\0';
    drive->serial[0] = '\0';

    u16 io = drive->io_base;

    ata_select_drive(drive->io_base, drive->control_port, drive->slave);

    if (!ata_wait_not_busy(io)) {
        drive->type = ATA_DEVICE_NONE;
        return false;
    }

    outb(io + ATA_REG_SECTOR_COUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    u8 status = inb(io + ATA_REG_STATUS);

    if (status == 0 || status == 0xFF) {
        drive->type = ATA_DEVICE_NONE;
        return false;
    }

    bool ready = false;

    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        status = inb(io + ATA_REG_STATUS);

        if (status == 0 || status == 0xFF)
          return false;

       if (status & ATA_SR_BSY)
          continue;

       if (status & ATA_SR_DF)
          return false;

       if (status & ATA_SR_ERR)
           break;  // Leave loop to inspect the ATAPI signature


        if (status & ATA_SR_DRQ) {
            ready = true;
            break;
        }

    }


   u8 signature_mid = inb(io + ATA_REG_LBA_MID);
   u8 signature_high = inb(io + ATA_REG_LBA_HIGH);

    bool is_atapi =
      signature_mid == 0x14 && signature_high == 0xEB;

  
    if (status & ATA_SR_ERR) {
        if (is_atapi)
            drive->type = ATA_DEVICE_ATAPI;

        return false;
    }

    if (!ready || !(status & ATA_SR_DRQ))
        return false;

  
    if (signature_mid != 0 || signature_high != 0)
        return false;


    for (u32 i = 0; i < 256; ++i)
      identify[i] = inw(io + ATA_REG_DATA);


    drive->supports_lba28 =
      (identify[49] & (1u << 9)) != 0;


    bool word83_valid =
      (identify[83] & 0xC000) == 0x4000;

    drive->supports_lba48 =
      word83_valid &&
      ((identify[83] & (1u << 10)) != 0);




    u64 lba28_count =
      (u64)identify[60] |
      ((u64)identify[61] << 16);



    u64 lba48_count =
       (u64)identify[100] |
       ((u64)identify[101] << 16) |
       ((u64)identify[102] << 32) |
       ((u64)identify[103] << 48);

    if (drive->supports_lba48 && lba48_count != 0)
        drive->sector_count = lba48_count;
    else if (drive->supports_lba28)
        drive->sector_count = lba28_count;
    else
        drive->sector_count = 0;



    for (u32 i = 0; i < 10; ++i) {
        u16 word = identify[10 + i];
        drive->serial[i * 2] = (char)(word >> 8);
        drive->serial[i * 2 + 1] = (char)word;
    }

    drive->serial[20] = '\0';


    for (u32 i = 0; i < 20; ++i) {
        u16 word = identify[27 + i];
        drive->model[i * 2] = (char)(word >> 8);
        drive->model[i * 2 + 1] = (char)word;
    }

    drive->model[40] = '\0';

    drive->type = ATA_DEVICE_ATA;
    return true;
}



bool atapi_identify(ata_drive_t *drive, u16 identify[256]) {
    drive->type = ATA_DEVICE_NONE;
    drive->supports_lba28 = false;
    drive->supports_lba48 = false;
    drive->sector_count = 0;
    drive->serial[0] = '\0';
    drive->model[0] = '\0';

    u16 io = drive->io_base;

    ata_select_drive(drive->io_base, drive->control_port, drive->slave);

    if (!ata_wait_not_busy(io)) 
        return false;
    
    u8 signature_mid = inb(io + ATA_REG_LBA_MID);
    u8 signature_high = inb(io + ATA_REG_LBA_HIGH);
    

    if (signature_mid != 0x14 || signature_high != 0xEB)
      return false;


    outb(io + ATA_REG_SECTOR_COUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

    u8 status = 0;
    bool ready = false;
    
    for (u32 timeout = 0; timeout < 1000000; ++timeout) {
        status = inb(io + ATA_REG_STATUS);

        if (status == 0 || status == 0xFF)
          return false;

        if (status & ATA_SR_BSY)
          continue;

        if (status & (ATA_SR_DF | ATA_SR_ERR))
          return false;

        if (status & ATA_SR_DRQ) {
            ready = true;
            break;
        }
    }

    if (!ready)
        return false;


    for (u32 i = 0; i < 256; ++i)
      identify[i] = inw(io + ATA_REG_DATA);


    for (u32 i = 0; i < 10; ++i) {
        u16 word = identify[10 + i];
        drive->serial[i * 2] = (char)(word >> 8);
        drive->serial[i * 2 + 1] = (char)word;
    }

    drive->serial[20] = '\0';


    for (u32 i = 0; i < 20; ++i) {
        u16 word = identify[27 + i];
        drive->model[i * 2] = (char)(word >> 8);
        drive->model[i * 2 + 1] = (char)word;
    }

    drive->model[40] = '\0';

    drive->supports_lba28 = false;
    drive->supports_lba48 = false;
    drive->sector_count = 0;
    drive->type = ATA_DEVICE_ATAPI;
    return true;
}
