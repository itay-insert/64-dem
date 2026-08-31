#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/va_alloc_internal.h"

#define USER_SPACE 0x40000000
#define USER_END 0x7FFFFFFFFFFF

extern u64 Kernel_end;

#define END 0xFFFF8FFFFFFFFFFF
#define KERNEL_SPACE 0xFFFFC00000000000

#define SPACE_END 0xFFFFFFFFFFFFFFFF

static inline va_node *find_new_entry(void) {
    va_node *entry = NULL;
    va_hd *hd = va_header;
    while (hd->free_entries == NoEntriesLeft) {
        if (hd->next_page == NULL) {
            EFI_MEMORY_DESCRIPTOR alloc = vmalloc(va_top, 1, 0x03);
            if (alloc.Attribute != 0)
                return NULL;
            va_hd *new_hd = (va_hd *)va_top;
            va_top += 0x1000;
            metadata_pages++;
            new_hd->free_entries = (4096 - sizeof(va_hd)) / sizeof(va_node);
            new_hd->next_page = NULL;
            hd->next_page = new_hd;
            entry = (va_node *)((u8 *)va_hd + sizeof(va_hd));
        }
        hd = hd->next_page;
    }

    if (entry == NULL) {
        entry = (va_node *)((u8 *)va_latest + sizeof(va_node));
    }

    return entry;
}

va_ret va_alloc(u64 pages, u16 attributes) {
    va_ret ret = {0};
    spin_lock(&va_lock);

    if (va_header == NULL) {
        EFI_MEMORY_DESCRIPTOR alloc = vmalloc(VA_TREE, 1, 0x03);
        if (alloc.Attribute != 0) {
            spin_unlock(&va_lock);
            ret.status = 1;
            return ret;
        }
        va_top += 0x1000;
        va_header = (va_hd *)VA_TREE;
        va_header->free_entries = (4096 - sizeof(va_hd)) / sizeof(va_node);
        va_header->next_page = NULL;
    }

    if (entries == 0) {
        va_start = (va_node *)((u8 *)va_header + sizeof(va_node));
        va_start->attributes = 0x03;
        va_start->virtual_base = (Kernel_end + 4095) & ~0xFFF;
        va_start->length = (END - va_start->virtual_base) + 1;
        va_latest = va_start;
        
    }
}
