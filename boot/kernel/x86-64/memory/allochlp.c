#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/malloc_internal.h"


heap_entry *find_new_entry(void) {
    heap_entry *entry = NULL;
    heap_entry *hd = heap_header;
    while (hd->SizeInBytes == NoEntriesLeft) {
        if (hd->next_entry == NULL) {
            EFI_MEMORY_DESCRIPTOR allocation = vmalloc(heap_top, 1, 0x03);
            if (allocation.Attribute != 0)
                return NULL;
            heap_entry *new_header = (heap_entry *)heap_top;
            heap_top += 0x1000;
            metadata_pages++;
            memset(new_header, 0, sizeof(heap_entry));
            new_header->status = Header;
            new_header->SizeInBytes = (4096 / sizeof(heap_entry)) - 1;
            new_header->next_entry = NULL;
            hd->next_entry = new_header;
            entry = (heap_entry *)((u8 *)new_header + sizeof(heap_entry));
        }
        hd = hd->next_entry;
    }

    if (entry == NULL) {
        entry = (heap_entry *)((u8 *)heap_latest + sizeof(heap_entry));
    }

    return entry;
   
}