#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "memory.h"
#include "lowlevel.h"

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

typedef struct {
    u16 attributes;
    u64 AddressOfPdpt;
} PML4;

typedef struct {
    u16 attributes;
    u64 AddressOfPd;
} PDPT;

typedef struct {
    u16 attributes;
    u64 AddressOfPt;
} PD;

typedef struct {
    u16 attributes;
    u64 AddressOfPage;
} PT;

int GbPageSupport = 0;


u64 alloc_pages(u64 pages) {
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(pages);
    u8 *region = (u8 *)alloc.PhysicalStart;
    for (u64 i = 0; i < (pages << 12); i++) {
        region[i] = 0;
    }
    return alloc.PhysicalStart;
}


u64 create_mapping(u64 address, u64 pages, u16 attributes) {
    u64 PML4_base = alloc_pages(1); // alloc pages zero-intiallizes
    int PT_entries = (int)(pages & 511);
    int PD_entries =  (int)((pages >> 9) & 511);
    int PDPT_entries = (int)((pages >> 18) & 511);
    int PML4_entries = (int)((pages >> 27) & 511);

}

void SetupPaging(PAGING_SETUP_DESCRIPTOR ps) {
    GbPageSupport = check_1gb_PageSupport();
    allocator_init(ps.bitmap, ps.memory_map, MemoryMapSize, DescriptorSize, 
    KernelStart, KernelEnd, BitmapSize);
}