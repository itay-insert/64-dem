#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "memory.h"

#ifndef PAGING_H
#define PAGING_H

#define fb_virtual 0xffffa00000000000
#define kernel_virtual 0xffff800000000000
#define BASE 0xffff900000000000

typedef struct {
    u64 *info_buffer64;
    int *info_buffer;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;

extern int GbPageSupport;
extern u64 KernelPML4;

void flush_pages(u64 virtual_address, u64 pages);
void create_mapping(u64 virtual_address, u64 physical_address, u64 pages, u16 attributes, u64 *PML4);
void SetupPaging(PAGING_SETUP_DESCRIPTOR ps);


#endif