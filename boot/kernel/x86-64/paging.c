#include <stdbit.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "memory.h"




typedef struct {
    u64 kernel_size;
    u64 fb_size;
    u64 kernel_base;
    u64 fb_base;
    u64 kernel_end;
    EFI_MEMORY_DESCRIPTOR *memory_map;
    u8 *bitmap;
    u64 memory_map_size;
    u64 DescriptorSize;
    u64 bitmap_size;
} PAGING_SETUP_DESCRIPTOR;

void SetupPaging(PAGING_SETUP_DESCRIPTOR ps) {
    allocator_init(ps.bitmap, ps.memory_map, ps.memory_map_size, ps.DescriptorSize, ps.kernel_base, ps.kernel_end, ps.bitmap_size);
}