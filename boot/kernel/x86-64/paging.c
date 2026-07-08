#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "memory.h"

#define KernelEntry ps.info_buffer64[0]
#define KernelStart ps.info_buffer64[1]
#define KernelEnd ps.info_buffer64[2]
#define Framebuffer_base ps.info_buffer64[3]
#define MemoryMapSize ps.info_buffer64[4]
#define DescriptorSize ps.info_buffer64[5]
#define BitmapSize ps.info_buffer64[6]
#define PixelMode ps.info_buffer[0]
#define Horizontal_res ps.info_buffer[1]
#define Vertical_res ps.info_buffer[2]
#define PixelsPerScanline ps.info_buffer[3]


typedef struct {
    u64 *info_buffer64;
    int *info_buffer;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;



void SetupPaging(PAGING_SETUP_DESCRIPTOR ps) {
    allocator_init(ps.bitmap, ps.memory_map, MemoryMapSize, DescriptorSize, 
    KernelStart, KernelEnd, BitmapSize);

}