#ifndef MEMORY_H
#define MEMORY_H
#include <stddef.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"


u64 calculate_pages(EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize);
int memcmp(const void *ptr1, const void *ptr2, size_t count);
u8 check_byte(u8 byte, u8 bit, u8 dir);
u8 set_bit(u8 byte, u8 bit, u8 value, u8 dir);
void allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size);
EFI_MEMORY_DESCRIPTOR alloc_frame(u64 PageCount);
void free_frame(EFI_MEMORY_DESCRIPTOR frame);
void SetBitmapBase(u8 *bitmap);
EFI_MEMORY_DESCRIPTOR kmalloc(u64 virtual_address, u64 pages);
void kfree(EFI_MEMORY_DESCRIPTOR allocation);

#endif