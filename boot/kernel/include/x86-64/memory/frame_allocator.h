#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include "uint_definitions.h"
#include "x86-64/efi_memory_types.h"

extern u64 bitmap_base;
extern u64 bitmapSize;
extern u64 former_count;

void allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size);
EFI_MEMORY_DESCRIPTOR alloc_frame(u64 PageCount);
void free_frame(EFI_MEMORY_DESCRIPTOR frame);

#endif
