#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/malloc_internal.h"



void *kmalloc(size_t size) {

    if (heap_header == NULL) {
        EFI_MEMORY_DESCRIPTOR allocation = vmalloc(HEAP, 1, 0x03);
        if (allocation.Attribute != 0)   
            return NULL;  // check allocation failure, return NULL for failure
        
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
        EFI_MEMORY_DESCRIPTOR allocation = vmalloc(free_kbase, ((size + 4095) >> 12), 0x03);
        if (allocation.Attribute != 0)
            return NULL;
        heap_start->virtual_base = allocation.VirtualStart;
        heap_start->SizeInBytes = size;
        free_kbase += size;
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
        

        return (void *)heap_start->virtual_base;
    }


    heap_entry *entry = heap_start;
    heap_entry *free_entry = NULL;
    while (entry != NULL) {
        if (entry->status == Free && entry->SizeInBytes >= size && entry->attributes == 0x03) {
            if (entry->SizeInBytes == size) {
                entry->status = Used;
                return (void *)entry->virtual_base;
            } else if (entry->SizeInBytes > size) {
                u64 rem = entry->SizeInBytes - size; // rem stands for "remainder"
                u64 nb = entry->virtual_base + entry->SizeInBytes //
            }
        }
    }
}