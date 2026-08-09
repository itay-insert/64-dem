#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "boot_info.h"

#ifndef PAGING_H
#define PAGING_H

#define fb_virtual 0xffffa00000000000
#define kernel_virtual 0xffff800000000000
#define BASE 0xffff900000000000
#define PCI_BASE 0xffffa010e0000000
#ifndef DMA_BASE
#define DMA_BASE 0xffffa0ffc0000000
#endif
#ifndef DMA_POOL
#define DMA_POOL 0xffffa10000000000
#endif

typedef struct {
    u64 physical_address;
    u16 attributes;
    int Page_Type;
    int status;
} PAGING_LOOKUP_DESCRIPTOR;


typedef struct {
    BOOT_INFO64 *info64;
    BOOT_INFO32 *info32;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;

extern int GbPageSupport;
extern u64 KernelPML4;

void flush_pages(u64 virtual_address, u64 pages);
void create_mapping(u64 virtual_address, u64 physical_address, u64 pages, u16 attributes, u64 *PML4);
PAGING_LOOKUP_DESCRIPTOR paging_lookup(u64 virtual_address, u64 *PML4);
void SetupPaging(PAGING_SETUP_DESCRIPTOR ps);


#endif
