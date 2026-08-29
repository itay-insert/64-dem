#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/memory_mapping.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/spinlock.h"

extern spinlock_t bitmap_lock;

EFI_MEMORY_DESCRIPTOR vmalloc(u64 virtual_address, u64 pages, u16 attributes) {
    spin_lock(&bitmap_lock);
    EFI_MEMORY_DESCRIPTOR ret = {0};
    u8 *bitmap = (u8 *)bitmap_base;
    if (pages == 0) {
        ret.Attribute = 2;
        spin_unlock(&bitmap_lock);
        return ret;
    }
    u64 count = former_count;
    u64 count_tar = count;
    u64 count_virt = virtual_address;
    u64 match_count = 0;
    u64 PageCount = pages;
    if (run_simulation(count, PageCount, bitmap) == 1) {
        ret.Attribute = 1;
        spin_unlock(&bitmap_lock);
        return ret;
    }
    while (PageCount > 0) {
        if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 0) {
            match_count++;
            count++;
            PageCount--;
        } else if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) {
            fill_bitmap(count_tar, match_count, bitmap);
            create_mapping(count_virt, count_tar<<12, match_count, attributes, KernelPML4);
            count_virt += (match_count << 12);
            match_count = 0;
            while (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) count++;
            count_tar = count;
        }
    }
    fill_bitmap(count_tar, match_count, bitmap);
    create_mapping(count_virt, count_tar<<12, match_count, attributes, KernelPML4);
    PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(virtual_address, KernelPML4);
    ret.PhysicalStart = lookup.physical_address;
    ret.VirtualStart = virtual_address;
    ret.Attribute = 0;
    ret.NumberOfPages = pages;
    flush_pages(virtual_address, pages);
    former_count = count;
    spin_unlock(&bitmap_lock);
    return ret;
}

void vfree(EFI_MEMORY_DESCRIPTOR allocation) {
    u64 virtual_base = allocation.VirtualStart;
    u64 pages = allocation.NumberOfPages;
    while (allocation.NumberOfPages > 0) {
        EFI_MEMORY_DESCRIPTOR frame = {0};
        frame.VirtualStart = allocation.VirtualStart;
        PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(allocation.VirtualStart, KernelPML4);
        frame.PhysicalStart = lookup.physical_address;
        if (lookup.Page_Type == 0) {
            frame.NumberOfPages = 1;
            free_frame(frame);
            allocation.NumberOfPages--;
            allocation.VirtualStart += 0x1000;
        } else if (lookup.Page_Type == 1) {
            frame.NumberOfPages = 512;
            free_frame(frame);
            allocation.NumberOfPages -= 512;
            allocation.VirtualStart += 0x200000;
        } else if (lookup.Page_Type == 2) {
            frame.NumberOfPages = 262144;
            free_frame(frame);
            allocation.NumberOfPages -= 262144;
            allocation.VirtualStart += 0x40000000;
        }
    }

    destroy_mapping(virtual_base, pages, KernelPML4);
    flush_pages(virtual_base, pages);
}
