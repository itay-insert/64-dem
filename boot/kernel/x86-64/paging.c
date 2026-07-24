#include <stdint.h>
#include "uint_definitions.h"
#include "efi_memory_types.h"
#include "lowlevel.h"
#include "vga.h"
#include "memory.h"
#include "boot_info.h"


#define fb_virtual 0xffffa00000000000
#define kernel_virtual 0xffff800000000000
#define BASE 0xffff900000000000
#define PCI_BASE 0xffffa010e0000000


typedef struct {
    u64 physical_address;
    u16 attributes;
    int Page_Type;
    int status;
} PAGING_LOOKUP_DESCRIPTOR;

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

void flush_pages(u64 virtual_address, u64 pages) {
    u64 virtual_end = virtual_address + (4096 * pages);

    while (virtual_address < virtual_end)
    {
        asm volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
        virtual_address += 4096;
    }
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

PAGING_LOOKUP_DESCRIPTOR paging_lookup(u64 virtual_address, u64 *PML4) {
    PAGING_LOOKUP_DESCRIPTOR ret = {0};
    int pt_index = (virtual_address >> 12) & 511;
    int pd_index = (virtual_address >> 21) & 511;
    int pdpt_index = (virtual_address >> 30) & 511;
    int pml4_index = (virtual_address >> 39) & 511;
    if (PML4[pml4_index] == 0) {
        ret.status = 1; // 0 = success, 1 = PDPT not found, 2 = PD not found, 3 = PT not found, 4 = pt_entry not valid, 5 = unknown
        return ret;
    }
    u64 *PDPT = (u64 *)(PML4[pml4_index] & ~0xfff);
    if (PDPT[pdpt_index] == 0) {
        ret.status = 2;
        return ret;
    }
    if ((PDPT[pdpt_index] & 0x80) != 0) {
        ret.attributes = (PDPT[pdpt_index] & 0xfff) & ~0x80;
        ret.physical_address = PDPT[pdpt_index] & ~0xfffULL;
        ret.Page_Type = 2; // 2 = GB page, 1 = 2MB page, 0 = regular page
        ret.status = 0;
        return ret;
    } else {
        u64 *PD = (u64 *)(PDPT[pdpt_index] & ~0xfff);
        if (PD[pd_index] == 0) {
            ret.status = 3;
            return ret;
        }
        if ((PD[pd_index] & 0x80) != 0) {
            ret.attributes = (PD[pd_index] & 0xfff) & ~0x80;
            ret.physical_address = PD[pd_index] & ~0xfffULL;
            ret.Page_Type = 1; // 2 = GB page, 1 = 2MB page, 0 = regular page
            ret.status = 0;
            return ret;
        } else {
            u64 *PT = (u64 *)(PD[pd_index] & ~0xfff);
            if (PT[pt_index] == 0) {
                ret.status = 4;
                return ret;
            }
            ret.attributes = (PT[pt_index] & 0xfff) & ~0x80;
            ret.physical_address = PT[pt_index] & ~0xfffULL;
            ret.Page_Type = 0; // 2 = GB page, 1 = 2MB page, 0 = regular page
            ret.status = 0;
            return ret;
        }
    }

}

typedef struct {
    BOOT_INFO64 *info64;
    BOOT_INFO32 *info32;
    u8 *bitmap;
    EFI_MEMORY_DESCRIPTOR *memory_map;
} PAGING_SETUP_DESCRIPTOR;

u64 KernelPML4 = 0;

void SetupPaging(PAGING_SETUP_DESCRIPTOR ps) {
    GbPageSupport = check_1gb_PageSupport();
    qemu_debug_print("[paging] CPUID check complete\n");
    qemu_debug_print("[paging] bitmap base=");
    qemu_debug_hex_u64((u64)ps.bitmap);
    qemu_debug_print(" size=");
    qemu_debug_hex_u64(ps.info64->bitmap_size);
    qemu_debug_print(" mmap size=");
    qemu_debug_hex_u64(ps.info64->memory_map_size);
    qemu_debug_print(" descriptor size=");
    qemu_debug_hex_u64(ps.info64->descriptor_size);
    qemu_debug_print("\n");
    allocator_init(ps.bitmap, ps.memory_map, ps.info64->memory_map_size,
                   ps.info64->descriptor_size, ps.info64->kernel_start,
                   ps.info64->kernel_end, ps.info64->bitmap_size);
    qemu_debug_print("[paging] allocator initialized\n");
    PML4_base = alloc_pages(1);
    qemu_debug_print("[paging] PML4 allocated\n");
    u64 *PML4 = (u64 *)PML4_base;
    qemu_debug_print("[paging] conventional memory mapped\n");
    for (u64 i = 0; i < (ps.info64->memory_map_size / ps.info64->descriptor_size); i++) {
        u8 *ptr = (u8 *)ps.memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * ps.info64->descriptor_size);
        if (desc->Type == EfiUnusableMemory)
            continue;

        if (desc->Type == EfiMemoryMappedIO || desc->Type == EfiMemoryMappedIOPortSpace) {
            create_mapping(BASE+desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x13, PML4);
        } else if (desc->Type == EfiConventionalMemory || desc->Type == EfiPersistentMemory) {
            create_mapping(desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x03, PML4);
        } else if (desc->Type == EfiReservedMemoryType) {
            create_mapping(BASE+desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x03, PML4);
        } else if (desc->Type == EfiACPIReclaimMemory || desc->Type == EfiACPIMemoryNVS) {
            create_mapping(BASE+desc->PhysicalStart, desc->PhysicalStart, desc->NumberOfPages, 0x03, PML4);
        }
    }
    qemu_debug_print("[paging] firmware memory mapped\n");

    create_mapping(BASE+0xE0000, 0xE0000, 32, 0x03, PML4);
    qemu_debug_print("[paging] mapped bios legacy area\n");

    create_mapping(fb_virtual, ps.info64->framebuffer_base, (((ps.info32->vertical_resolution*ps.info32->pixels_per_scanline*4)+4095)>>12), 0x13, PML4);
    qemu_debug_print("[paging] framebuffer mapped\n");
    ps.info64->framebuffer_base = fb_virtual;

    u64 stack = (u64)ps.bitmap;
    u64 mmp = (u64)ps.memory_map;


    u64 pages = ((stack - ps.info64->kernel_start) >> 12) - 65;
    create_mapping(kernel_virtual, ps.info64->kernel_start, pages, 0x03, PML4);

    create_mapping(kernel_virtual+((pages+1)<<12), ps.info64->kernel_start+((pages+1)<<12), 64, 0x03, PML4);

    qemu_debug_print("[paging] kernel mapped\n");

    create_mapping(kernel_virtual+(stack-ps.info64->kernel_start), stack, ((ps.info64->bitmap_size + 4095) >> 12), 0x03, PML4);

    create_mapping(kernel_virtual+(mmp-ps.info64->kernel_start), mmp, (((ps.info64->memory_map_size+ps.info32->info_size) + 4095)>>12), 0x03, PML4);

    ps.info64->device_path =
        kernel_virtual + (ps.info64->device_path - ps.info64->kernel_start);
    ps.info64->kernel_end = kernel_virtual + (ps.info64->kernel_end - ps.info64->kernel_start);
    ps.info64->kernel_entry = kernel_virtual + (ps.info64->kernel_entry - ps.info64->kernel_start);
    u64 ib_addr = (u64)ps.info32;
    u64 ib64_addr = (u64)ps.info64;
    stack = kernel_virtual + (stack - ps.info64->kernel_start);
    mmp = kernel_virtual + (mmp - ps.info64->kernel_start);
    ib_addr = kernel_virtual + (ib_addr - ps.info64->kernel_start);
    ib64_addr = kernel_virtual + (ib64_addr - ps.info64->kernel_start);
    u8 *virtual_bitmap = (u8 *)stack;
    SetBitmapBase(virtual_bitmap);
    ps.info64->kernel_start = kernel_virtual;
    EFI_MEMORY_DESCRIPTOR *virtual_mmp = (EFI_MEMORY_DESCRIPTOR *)mmp;
    BOOT_INFO32 *ib = (BOOT_INFO32 *)ib_addr;
    BOOT_INFO64 *ib64 = (BOOT_INFO64 *)ib64_addr;


    KernelPML4 = PML4_base;
    qemu_debug_print("[paging] loading CR3\n");
    enable_paging(KernelPML4);
    qemu_debug_print("[paging] CR3 loaded\n");

    qemu_debug_print("[paging] jumping to virtual kernel\n");
    kernel_trampoline(ib64, ib, stack, virtual_mmp, ib64->kernel_entry);
    

}
