#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/malloc_internal.h"







void *kmalloc(u64 pages) {
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


  
    spin_unlock(&heap_lock);
}