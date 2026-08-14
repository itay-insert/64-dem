#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/kernel_allocator.h"
#include "x86-64/memory/dma.h"
#include "x86-64/memory/dma_internal.h"



u64 allocate_dma(u64 size) {
    u64 *PML4 = (u64 *)KernelPML4;
    u64 pages = (size + sizeof(dma_descriptor) + 4095) >> 12;
    if (dma_header == NULL) {
        EFI_MEMORY_DESCRIPTOR allocation = kmalloc(DMA_POOL, 1);
        if (allocation.Attribute != 0) return 1;
        dma_top += 0x1000;
        dma_header = (dma_entry *)DMA_POOL;
        dma_header->status = Header;
        dma_header->SizeInPages = (4096 / sizeof(dma_entry)) - 1;
        limit = (int)dma_header->SizeInPages;
        dma_header->next_entry = NULL;
    }

    if (entries == 0) {
        dma_start = (dma_entry *)((u8 *)dma_header + sizeof(dma_entry));
        dma_start->next_entry = NULL;
        dma_start->SizeInPages = pages;
        dma_start->status = Used;
        dma_latest = dma_start;
        EFI_MEMORY_DESCRIPTOR frame = alloc_frame(pages);
        if (frame.Attribute != 0) return 1;
        frame.VirtualStart = DMA_BASE;
        create_mapping(DMA_BASE, frame.PhysicalStart, pages, 0x03, PML4);
        flush_pages(DMA_BASE, pages);
        entries++;
        dma_header->SizeInPages--;
        dma_descriptor *header = 
            (dma_descriptor *)(frame.VirtualStart+((pages<<12)-sizeof(dma_descriptor)));
        memcpy(header->sign, "DMA_POOL", 8);
        header->status = 1; // metadata for freeing the region
        header->SizeInPages = pages;
        header->home_entry = dma_start;

        return frame.VirtualStart;
    }

    u64 free_pages = 0;
    u64 free_base = DMA_BASE;
    u64 cursor = DMA_BASE;
    dma_entry *entry = dma_start;
    dma_entry *free_entry = NULL;
    while (entry != NULL) {
        if (entry->status == Free) {
            if (free_entry == NULL) {
                free_entry = entry;
                free_base = cursor;
            }
            free_pages += entry->SizeInPages;
            if (free_pages >= pages) break;
        } else {
            free_entry = NULL;
            free_pages = 0;
            free_base = cursor + (entry->SizeInPages << 12);
        }
        cursor += entry->SizeInPages << 12;
        entry = entry->next_entry;
    }


    int entries_used = 0;
    if (free_pages < pages) {
        entry = dma_header;
        int entries_limit = (int)entry->SizeInPages;
        while (entries_limit == NoEntriesLeft) {
            if (entry->next_entry == NULL) {
                EFI_MEMORY_DESCRIPTOR allocation = kmalloc(dma_top, 1);
                if (allocation.Attribute != 0) return 1;
                dma_entry *new_page = (dma_entry *)allocation.VirtualStart;
                dma_top += 0x1000;
                metadata_pages++;
                new_page->status = Header;
                new_page->next_entry = NULL;
                new_page->SizeInPages = limit;
                entry->next_entry = new_page;
                dma_latest->next_entry = (dma_entry *)((u8 *)new_page + sizeof(dma_entry));
                dma_latest = dma_latest->next_entry;
            }

            entry = entry->next_entry;
            entries_limit = (int)entry->SizeInPages;
        }
        if (entries_limit != limit || metadata_pages == 0) {
            dma_latest->next_entry = (dma_entry *)((u8 *)dma_latest + sizeof(dma_entry));
            dma_latest = dma_latest->next_entry;
        }
        entries++;
        entry->SizeInPages--;
        dma_latest->status = Used;
        dma_latest->SizeInPages = pages - free_pages;
        dma_latest->next_entry = NULL;
        entries_used++;
        if (free_pages > 0) {
            entry = free_entry;
            while (entry != dma_latest) {
                entry->status = Used;
                entries_used++;
                entry = entry->next_entry;
            }
        } else free_entry = dma_latest;
    } else {
        entry = free_entry;
        u64 claimed_pages = 0;
        while (claimed_pages < pages) {
            entry->status = Used;
            claimed_pages += entry->SizeInPages;
            entries_used++;
            entry = entry->next_entry;
        }
    }
    

    EFI_MEMORY_DESCRIPTOR allocation = alloc_frame(pages);
    if (allocation.Attribute != 0) return 1;
    allocation.VirtualStart = free_base;
    create_mapping(free_base, allocation.PhysicalStart, pages, 0x03, PML4);
    flush_pages(free_base, pages);
    dma_descriptor *header = 
        (dma_descriptor *)(allocation.VirtualStart+((pages<<12)-sizeof(dma_descriptor)));
    
    memcpy(header->sign, "DMA_POOL", 8);
    header->status = entries_used; // metadata for freeing the region
    header->SizeInPages = pages;
    header->home_entry = free_entry;

    return allocation.VirtualStart;

}




void free_dma(u64 Base) {
    u64 *PML4 = (u64 *)KernelPML4;
    dma_descriptor *ptr = (dma_descriptor *)find_descriptorBase(Base);
    dma_entry *pool_entry = ptr->home_entry;
    int entries_used = ptr->status;
    EFI_MEMORY_DESCRIPTOR allocation = {0};
    allocation.VirtualStart = Base;
    allocation.NumberOfPages = ptr->SizeInPages;
    PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(allocation.VirtualStart, PML4);
    allocation.PhysicalStart = lookup.physical_address;
    free_frame(allocation);

    for (int i = 0; i < entries_used; i++) {
        if (pool_entry->status != Header) {
            pool_entry->status = Free;
            dma_entry *page_header = find_header(pool_entry);
            page_header->SizeInPages++;
        }
        pool_entry = pool_entry->next_entry;
    }


    for (int j = metadata_pages; j > 0; j--) {
        pool_entry = dma_header;

        for (int i = 0; i < j; i++) {
            pool_entry = pool_entry->next_entry;
        }

        if (pool_entry->SizeInPages == (u64)limit) {
            dma_entry *first_entry = (dma_entry *)((u8 *)pool_entry + sizeof(dma_entry));
            dma_entry *previous_entry = dma_start;
            while (previous_entry->next_entry != first_entry) {
                previous_entry = previous_entry->next_entry;
            }
            dma_latest = previous_entry;
            dma_latest->next_entry = NULL;
            dma_entry *Header_before = (dma_entry *)((u8 *)pool_entry - 0x1000);
            Header_before->next_entry = NULL;
            allocation.VirtualStart = (u64)pool_entry;
            allocation.NumberOfPages = 1;
            metadata_pages--;
            dma_top -= 0x1000;
            entries = (metadata_pages + 1) * limit;
            kfree(allocation);
        }

    }

   


}
