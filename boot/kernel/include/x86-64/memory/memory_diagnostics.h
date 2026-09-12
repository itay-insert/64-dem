#ifndef MEMORY_DIAGNOSTICS_H
#define MEMORY_DIAGNOSTICS_H

#include "boot_info.h"
#include "x86-64/efi_memory_types.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

void print_physical_memory_stats(void);
void print_virtual_memory_stats(void);
void print_core_kernel_buffers(const BOOT_INFO64 *info64,
                               const BOOT_INFO32 *info32,
                               u64 bitmap_address,
                               const EFI_MEMORY_DESCRIPTOR *memory_map);
void print_compressed_memory_map(const BOOT_INFO64 *info64,
                                 const BOOT_INFO32 *info32,
                                 u64 bitmap_address,
                                 const EFI_MEMORY_DESCRIPTOR *memory_map);

KERNEL_EXTERN_C_END

#endif
