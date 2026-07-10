#include "uint_definitions.h"
#include "memory.h"

#ifndef LOWLEVEL_H
#define LOWLEVEL_H

u64 stack_get(void);
int check_1gb_PageSupport(void);
void enable_paging(u64 PML4_address);
void kernel_trampoline(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map, u64 entry);


#endif
