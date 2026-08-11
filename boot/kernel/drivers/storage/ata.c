#include <stdbool.h>
#include "uint.h"
#include "lowlevel.h"
#include "ports.h"
#include "memory.h"


#define P_CTL 0x3F6
#define S_CTL 0x376

#define PB 0x3F4
#define SB 0x374

#define Primary 0x1F0
#define Secondary 0x170

#define ATA_SR_ERR  0x01
#define ATA_SR_DRQ  0x08
#define ATA_SR_DF   0x20
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY  0x80

#define ATA_CTL_NIEN 0x02  
#define ATA_CTL_SRST 0x04
#define ATA_CTL_HOB  0x80  

#define ATA_CMD_IDENTIFY 0xEC



#define ATA_REG_DATA          0  
#define ATA_REG_ERROR         1  
#define ATA_REG_FEATURES      1  
#define ATA_REG_SECTOR_COUNT  2
#define ATA_REG_LBA_LOW       3
#define ATA_REG_LBA_MID       4
#define ATA_REG_LBA_HIGH      5
#define ATA_REG_DRIVE_HEAD    6
#define ATA_REG_STATUS        7  
#define ATA_REG_COMMAND       7  


static void ata_400ns_delay(u16 control_port) {
    inb(control_port);
    inb(control_port);
    inb(control_port);
    inb(control_port);
}



static bool ata_reset_channel(u16 control_port) {
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