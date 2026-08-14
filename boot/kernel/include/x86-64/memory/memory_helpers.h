#ifndef MEMORY_HELPERS_H
#define MEMORY_HELPERS_H

#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/efi_memory_types.h"

u64 calculate_pages(EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize);
int memcmp(const void *ptr1, const void *ptr2, size_t count);
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t n);
u8 check_byte(u8 byte, u8 bit, u8 dir);
u8 set_bit(u8 byte, u8 bit, u8 value, u8 dir);

#endif
