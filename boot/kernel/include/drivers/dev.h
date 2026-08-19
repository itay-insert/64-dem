#ifndef DEV_H
#define DEV_H

#include <stdbool.h>
#include "drivers/devdef.h"


typedef struct {
    bool ATA;
    bool AHCI;
    bool NVMe;
    int USB;
} device_vendors;


extern device_vendors devices;

#endif