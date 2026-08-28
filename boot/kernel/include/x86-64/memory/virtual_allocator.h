#ifndef VIRTUAL_ALLOCATOR_H
#define VIRTUAL_ALLOCATOR_H

#include "uint_definitions.h"
#include "x86-64/efi_memory_types.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

EFI_MEMORY_DESCRIPTOR vmalloc(u64 virtual_address, u64 pages, u16 attributes);
void vfree(EFI_MEMORY_DESCRIPTOR allocation);

KERNEL_EXTERN_C_END

#endif
