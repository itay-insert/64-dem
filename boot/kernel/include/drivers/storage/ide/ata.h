#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include "uint_definitions.h"

#include "drivers/storage/ide/atadef.h"

typedef enum {
    ATA_DEVICE_NONE,
    ATA_DEVICE_ATA,
    ATA_DEVICE_ATAPI
} ata_device_type_t;

typedef enum {
    ATA_ADDRESSING_INVALID,
    ATA_ADDRESSING_LBA28,
    ATA_ADDRESSING_LBA48
} ata_addressing_mode_t;

typedef struct {
    u16 io_base;
    u16 control_port;
    bool slave;

    ata_device_type_t type;
    bool supports_lba28;
    bool supports_lba48;
    u64 sector_count;

    char model[41];
    char serial[21];

    bool ata_status;
} ata_drive_t;


void ata_400ns_delay(u16 control_port);
bool ata_wait_not_busy(u16 io_base);
bool ata_poll_drq(ata_drive_t *drive);
bool ata_reset_channel(u16 control_port);
void ata_select_drive(u16 io_base, 
u16 control_port, bool slave);
bool ata_identify(ata_drive_t *drive, u16 identify[256]);
bool atapi_identify(ata_drive_t *drive, u16 identify[256]);
ata_addressing_mode_t ata_configure_lba(ata_drive_t *drive, u64 lba, u8 sector_count);

#endif
