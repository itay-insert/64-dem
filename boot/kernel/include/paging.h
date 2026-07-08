#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "memory.h"

#ifndef PAGING_H
#define PAGING_H

typedef struct {
    u64 *info_buffer64;
    int *info_buffer;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;



void SetupPaging(PAGING_SETUP_DESCRIPTOR ps);


#endif