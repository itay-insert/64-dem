#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/malloc_internal.h"






static inline u64 entry_init(heap_entry *entry) {
    memset(entry->virtual_base, 0, entry->SizeInBytes);
    heap_entry *hd = entry->virtual_base + sizeof(heap_entry);
    hd->attributes = entry->attributes;
    hd->virtual_base = entry->virtual_base;
    hd->SizeInBytes = entry->SizeInBytes;
    hd->status = entry->status;
    hd->next_entry = entry;
    return (entry->virtual_base + sizeof(heap_entry));
}

void *kmalloc(size_t size) {
    u64 sz = size + sizeof(heap_entry);
    spin_lock(&heap_lock);

    if (heap_header == NULL) {
        EFI_MEMORY_DESCRIPTOR allocation = vmalloc(HEAP, 1, 0x03);
        if (allocation.Attribute != 0)  {
            spin_unlock(&heap_lock);
            return NULL;
        }   // check allocation failure, return NULL for failure
        
        heap_top += 0x1000;
        heap_header = (heap_entry *)HEAP;
        memset(heap_header, 0, sizeof(heap_entry)); // zero initialize the header
        heap_header->status = Header;
        heap_header->SizeInBytes = (4096 / sizeof(heap_entry)) - 1; // for the header, size in bytes describes the free entry capacity
        limit = (int)heap_header->SizeInBytes;
        heap_header->next_entry = NULL;
    }


    if (entries == 0) {
        heap_start = (heap_entry *)((u8 *)heap_header + sizeof(heap_entry));   // create the first entry if it doesn't exist
        heap_start->status = Used;
        heap_start->attributes = 0x03;
        EFI_MEMORY_DESCRIPTOR allocation = vmalloc(free_kbase, ((sz + 4095) >> 12), 0x03);
        if (allocation.Attribute != 0) {
            spin_unlock(&heap_lock);
            return NULL;
        }
        heap_start->virtual_base = allocation.VirtualStart;
        heap_start->SizeInBytes = sz;
        free_kbase += sz;
        entries++;
        heap_header->SizeInBytes--;
        heap_latest = heap_start;
        if ((sz & 0xFFF) != 0) {
            heap_entry *new_entry = (heap_entry *)((u8 *)heap_start + sizeof(heap_entry));  // create another entry for the free space in the allocated page if remains
            new_entry->status = Free;
            new_entry->SizeInBytes = 0x1000 - (sz & 0xFFF);
            new_entry->attributes = 0x03;
            new_entry->virtual_base = free_kbase;
            new_entry->next_entry = NULL;
            heap_start->next_entry = new_entry;
            entries++;
            heap_header->SizeInBytes--;
            heap_latest = new_entry;
        } else
            heap_start->next_entry = NULL;
        
        spin_unlock(&heap_lock);
        memset()
        return (void *)heap_start->virtual_base;
    }


    heap_entry *entry = heap_start;
    heap_entry *free_entry = NULL;
    while (entry != NULL) {
        if (entry->status == Free && entry->SizeInBytes >= sz && entry->attributes == 0x03) {
            if (entry->SizeInBytes == sz) {
                entry->status = Used;
                spin_unlock(&heap_lock);
                return (void *)entry->virtual_base;
            } else if (entry->SizeInBytes > sz) {
                u64 rem = entry->SizeInBytes - sz; // rem stands for "remainder"
                u64 nb = entry->virtual_base + (entry->SizeInBytes - rem); // nb stands for new virtual base
                entry->SizeInBytes -= rem;
                entry->status = Used;
                
            }
        }
    }

    spin_unlock(&heap_lock);
}