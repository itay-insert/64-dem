#ifndef ATADEF_H
#define ATADEF_H

#define SUCCESS 0
#define ERR_INVALID_COUNT 1

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

#define ATA_CMD_IDENTIFY_PACKET 0xA1

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

#define ATA_CMD_FLUSH 0xE7
#define ATA_CMD_FLUSH_EXT 0xEA

#define ATA_CMD_WRITE 0x30
#define ATA_CMD_WRITE_EXT 0x34

#define ATA_CMD_READ 0x20
#define ATA_CMD_READ_EXT 0x24


#endif