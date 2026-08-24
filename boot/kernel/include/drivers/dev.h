#ifndef DEV_H
#define DEV_H

#include <stdbool.h>
#include "drivers/devdef.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

typedef struct {
    bool ATA;
    bool AHCI;
    bool NVMe;
    int USB;
} device_vendors;


extern device_vendors devices;

KERNEL_EXTERN_C_END

#endif
