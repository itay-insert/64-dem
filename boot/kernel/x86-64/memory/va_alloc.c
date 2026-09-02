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

#define USR (attributes & 0x04)

typedef enum {
    left = 0,
    right = 1,
    NoParent = 2,
};

static inline va_node *find_new_entry(u8 Pos, va_node *Parent) {
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
            entry = (va_node *)((u8 *)new_hd + sizeof(va_hd));
        }
        hd = hd->next_page;
    }

    if (entry == NULL) {
        entry = (va_node *)((u8 *)va_latest + sizeof(va_node));
    }

    entry->va_right = NULL;
    entry->va_left = NULL;

    entry->Parent = Parent;


    if (Pos == right) {
        Parent->va_right = entry;
    } else if (Pos == left) {
        Parent->va_left = entry;
    }

    va_latest = entry;
    entries++;
    hd->free_entries--;

    return entry;
}



va_ret va_alloc(u64 pages, u16 attributes) {
    va_ret ret = {0};
    if (pages == 0) {
        ret.status = 1;
        return ret;
    }
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
        va_kstart = (va_node *)((u8 *)va_header + sizeof(va_hd));
        entries++;
        va_header->free_entries--;
        va_kstart->va_left = NULL;
        va_kstart->va_right = NULL;
        va_kstart->Parent = NULL;
        va_kstart->attributes = 0x03; // for kernel
        va_kstart->virtual_base = (Kernel_end + 4095) & ~0xFFF;
        va_kstart->length = (END - va_kstart->virtual_base) + 1;
        va_latest = va_kstart;
        va_node *right_node = find_new_entry(right, va_kstart);
        right_node->virtual_base = KERNEL_SPACE;
        right_node->attributes = 0x03;
        right_node->length = (SPACE_END - KERNEL_SPACE) + 1;
        right_node->max_length = right_node->length;
        va_kstart->max_length = right_node->max_length;
        va_ustart = find_new_entry(NoParent, NULL);
        va_ustart->attributes = 0x07;  // for user
        va_ustart->virtual_base = USER_SPACE;
        va_ustart->length = (USER_END - USER_SPACE) + 1;
        va_ustart->max_length = va_ustart->length;

    }


    va_node *entry = NULL;

    u64 rsz = pages << 12;
    
    entry = USR ? va_ustart : va_kstart;

    

    while (1) {

        if (entry->length >= rsz) {
            EFI_MEMORY_DESCRIPTOR alloc = vmalloc(entry->virtual_base, pages, attributes);
            if (alloc.Attribute != 0) {
                spin_unlock(&va_lock);
                ret.status = 1;
                return ret;
            }
    
            
            if (entry->max_length == entry->length) {
                entry->max_length -= rsz;
            }

           
                
        
            entry->length -= rsz;

            entry->virtual_base += rsz;

            spin_unlock(&va_lock);

            ret.base = alloc.VirtualStart;
            ret.pages = pages;
            ret.attributes = attributes;
            ret.status = 0;
            return ret;
        } else if (entry->max_length >= rsz) {

            if (entry->va_left == NULL && entry->va_right == NULL) {
                spin_unlock(&va_lock);
                ret.status = 1;
                return ret;
            } else if (entry->va_left == NULL || entry->va_right == NULL) {
                if (entry->va_right != NULL) {
                    va_node *right_side = entry->va_right;
                    if (right_side->max_length >= rsz) {
                        entry = right_side;
                    } else {
                        spin_unlock(&va_lock);
                        ret.status = 1;
                        return ret;
                    }
                } else if (entry->va_left != NULL) {
                    va_node *left_side = entry->va_left;
                    if (left_side->max_length >= rsz) {
                        entry = left_side;
                    } else {
                        spin_unlock(&va_lock);
                        ret.status = 1;
                        return ret;
                    }
                }
            } else {
                va_node *left_side = entry->va_left;
                va_node *right_side = entry->va_right;

                if (left_side->max_length >= rsz) {
                    entry = left_side;
                } else if (right_side->max_length >= rsz) {
                    entry = right_side;
                } else {
                    spin_unlock(&va_lock);
                    ret.status = 1;
                    return ret;
                }
            }
        } else {
            spin_unlock(&va_lock);
            ret.status = 1;
            return ret;
        }

    }
    
}
