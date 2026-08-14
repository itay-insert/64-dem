#ifndef KERNEL_ALLOCATOR_H
#define KERNEL_ALLOCATOR_H

#include "uint_definitions.h"
#include "x86-64/efi_memory_types.h"

EFI_MEMORY_DESCRIPTOR kmalloc(u64 virtual_address, u64 pages);
void kfree(EFI_MEMORY_DESCRIPTOR allocation);

#endif
