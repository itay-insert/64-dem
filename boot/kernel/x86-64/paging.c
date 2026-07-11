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


#define fb_virtual 0xffffa00000000000
#define kernel_virtual 0xffff800000000000

int GbPageSupport = 0;

u64 PML4_base = 0;



u64 alloc_pages(u64 pages) {
    if (pages == 0) {
        return 0;
    }
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(pages);
    u8 *region = (u8 *)alloc.PhysicalStart;
    for (u64 i = 0; i < (pages << 12); i++) {
        region[i] = 0;
    }
    return alloc.PhysicalStart;
}

void free_pages(u64 address, u64 pages) {
    EFI_MEMORY_DESCRIPTOR free = {0, address, address, pages, 0};
    free_frame(free);
}


void create_mapping(u64 virtual_address, u64 physical_address, u64 pages, u16 attributes, u64 *PML4) {
    u64 virtual_end = virtual_address + (4096 * pages);
    while (virtual_address < virtual_end) {
        u64 size = virtual_end - virtual_address;
        int mb_enable = (virtual_address & 0x1fffff) | (physical_address & 0x1fffff);
        int gb_enable = (virtual_address & 0x3fffffff) | (physical_address & 0x3fffffff);
        u64 gb_count = size / 0x40000000;
        u64 mb_count = size / 0x200000;
        u64 kb_count = pages;
        int pt_index = (virtual_address >> 12) & 511;
        int pd_index = (virtual_address >> 21) & 511;
        int pdpt_index = (virtual_address >> 30) & 511;
        int pml4_index = (virtual_address >> 39) & 511;
        if (PML4[pml4_index] == 0) {
            PML4[pml4_index] = alloc_pages(1) | (attributes & 0x07); // alloc pages zero intiallizes
        }
        u64 *PDPT = (u64 *)((PML4[pml4_index] >> 12) << 12);
        if (gb_enable == 0 && GbPageSupport == 1 && gb_count > 0) {
            if ((PDPT[pdpt_index] & 0x80) == 0 && (PDPT[pdpt_index] & 1) == 1) {
                free_pages(((PDPT[pdpt_index] >> 12) << 12), 1);
            }
            if ((gb_count + pdpt_index) >= 512) {
                gb_count = 512 - pdpt_index;
            }
            for (u64 i = 0; i < gb_count; i++) {
                PDPT[pdpt_index+i] = physical_address | (attributes | 0x80);
                physical_address += 0x40000000;
                virtual_address += 0x40000000;
                pages -= 262144;
            }
        } else {
            if (PDPT[pdpt_index] == 0 || (PDPT[pdpt_index] & 0x80) != 0) {
                PDPT[pdpt_index] = alloc_pages(1) | (attributes & 0x07);
            }
            u64 *PD = (u64 *)((PDPT[pdpt_index] >> 12) << 12);
            if (mb_enable == 0 && mb_count > 0) {
                if ((PD[pd_index] & 0x80) == 0 && (PD[pd_index] & 1) == 1) {
                    free_pages(((PD[pd_index] >> 12) << 12), 1);
                }
                if ((mb_count + pd_index) >= 512) {
                    mb_count = 512 - pd_index;
                }
                for (u64 i = 0; i < mb_count; i++) {
                    PD[pd_index+i] = physical_address | (attributes | 0x80);
                    physical_address += 0x200000;
                    virtual_address += 0x200000;
                    pages -= 512;
                }
            } else {
                if (PD[pd_index] == 0 || (PD[pd_index] & 0x80) != 0) {
                    PD[pd_index] = alloc_pages(1) | (attributes & 0x07);
                }
                u64 *PT = (u64 *)((PD[pd_index] >> 12) << 12);
                if ((kb_count + pt_index) >= 512) {
                    kb_count = 512 - pt_index;
                }
                for (u64 i = 0; i < kb_count; i++) {
                    PT[pt_index+i] = physical_address | attributes;
                    physical_address += 4096;
                    virtual_address += 4096;
                    pages--;
                }
            }
        }
    }
}

typedef struct {
    u64 *info_buffer64;
    int *info_buffer;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;

u64 KernelPML4 = 0;

void SetupPaging(PAGING_SETUP_DESCRIPTOR ps) {
    GbPageSupport = check_1gb_PageSupport();
    allocator_init(ps.bitmap, ps.memory_map, MemoryMapSize, DescriptorSize, 
    KernelStart, KernelEnd, BitmapSize);
    PML4_base = alloc_pages(1);
    u64 *PML4 = (u64 *)PML4_base;
    for (u64 i = 0; i < (MemoryMapSize / DescriptorSize); i++) {
        u8 *ptr = (u8 *)ps.memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        if (desc->Type == EfiConventionalMemory) {
            create_mapping(
            desc->PhysicalStart,
            desc->PhysicalStart,
            desc->NumberOfPages,
            0x03,
            PML4
            );
        }
    }
    for (u64 i = 0; i < (MemoryMapSize / DescriptorSize); i++) {
        u8 *ptr = (u8 *)ps.memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        if (desc->Type == EfiMemoryMappedIO || desc->Type == EfiMemoryMappedIOPortSpace) {
            create_mapping(desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x13, PML4);
        } else if (desc->Type == EfiACPIMemoryNVS || desc->Type == EfiACPIReclaimMemory) {
            create_mapping(desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x03, PML4);
        }
    }

    create_mapping(fb_virtual, Framebuffer_base, (((Vertical_res*PixelsPerScanline*4)+4095)>>12), 0x13, PML4);
    Framebuffer_base = fb_virtual;

    create_mapping(kernel_virtual, KernelStart, (((KernelEnd-KernelStart)+4095)>>12), 0x03, PML4);

    KernelEnd = kernel_virtual + (KernelEnd - KernelStart);
    KernelEntry = kernel_virtual + (KernelEntry - KernelStart);
    u64 stack = (u64)ps.bitmap;
    u64 mmp = (u64)ps.memory_map;
    u64 ib_addr = (u64)ps.info_buffer;
    u64 ib64_addr = (u64)ps.info_buffer64;
    stack = kernel_virtual + (stack - KernelStart);
    mmp = kernel_virtual + (mmp - KernelStart);
    ib_addr = kernel_virtual + (ib_addr - KernelStart);
    ib64_addr = kernel_virtual + (ib64_addr - KernelStart);
    u8 *virtual_bitmap = (u8 *)stack;
    SetBitmapBase(virtual_bitmap);
    KernelStart = kernel_virtual;
    EFI_MEMORY_DESCRIPTOR *virtual_mmp = (EFI_MEMORY_DESCRIPTOR *)mmp;
    int *ib = (int *)ib_addr;
    u64 *ib64 = (u64 *)ib64_addr;


    KernelPML4 = PML4_base;
    enable_paging(KernelPML4);

    kernel_trampoline(ib64, ib, stack, virtual_mmp, KernelEntry);
    

}