#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include "uint_definitions.h"

typedef enum {
    ATA_DEVICE_NONE,
    ATA_DEVICE_ATA,
    ATA_DEVICE_ATAPI
} ata_device_type_t;

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
} ata_drive_t;

bool ata_identify(ata_drive_t *drive, u16 identify[256]);
bool atapi_identify(ata_drive_t *drive, u16 identify[256]);

#endif
