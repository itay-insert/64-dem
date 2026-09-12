#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/va_alloc_internal.h"
#include "x86-64/memory/va_alloc.h"

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
} va_labels;

typedef enum {
    RIGHT = 1 << 0,
    LEFT = 1 << 1,
} used_nodes;


static va_node *find_new_entry(u8 Pos, va_node *Parent) {
    va_node *entry = NULL;
    va_hd *hd = va_header;
    while (hd->free_entries == NoEntriesLeft) {
        if (hd->next_page == NULL) {
            EFI_MEMORY_DESCRIPTOR alloc = vmalloc(va_top, 1, 0x03);
            if (alloc.Attribute != 0)
                return NULL;
            va_hd *new_hd = (va_hd *)va_top;
            va_top += 0x1000;
            va_metadata_pages++;
            new_hd->free_entries = (4096 - sizeof(va_hd)) / sizeof(va_node);
            new_hd->unused_entries = 0;
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

    entry->UsedNodes = 0;

    if (Pos == right) {
        Parent->va_right = entry;
        Parent->UsedNodes |= RIGHT;
    } else if (Pos == left) {
        Parent->va_left = entry;
        Parent->UsedNodes |= LEFT;
    }

    va_latest = entry;
    va_entries++;
    hd->free_entries--;

    return entry;
}



static void UpdateParents(va_node *entry) {
    while (entry != NULL) {
        u64 max = entry->length;
        
        if (entry->va_left != NULL && entry->va_left->max_length > max) {
            max = entry->va_left->max_length;
        }

        if (entry->va_right != NULL && entry->va_right->max_length > max) {
            max = entry->va_right->max_length;
        }

        entry->max_length = max;
        entry = entry->Parent;
    }
}



static inline va_node *find_fit(va_node *entry, u64 size) {
    while (entry != NULL) {
        if (entry->length >= size) 
            return entry;


        if (entry->va_left != NULL && 
            entry->va_left->max_length >= size) {
                entry = entry->va_left;
                continue;
        } 

        if (entry->va_right != NULL &&
        entry->va_right->max_length >= size) {
            entry = entry->va_right;
            continue;
        }

        return NULL;

    }

    return NULL;
}


static inline va_hd *find_hd(va_node *entry) {
    u64 entry_base = (u64)entry & ~0xFFFULL;
    va_hd *enhd = (va_hd *)entry_base;
    return enhd;
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
        max = (int)va_header->free_entries;
        va_header->next_page = NULL;
        va_header->unused_entries = 0;
    }

    if (va_entries == 0) {
        va_kstart = (va_node *)((u8 *)va_header + sizeof(va_hd));
        va_entries++;
        va_header->free_entries--;
        va_kstart->va_left = NULL;
        va_kstart->va_right = NULL;
        va_kstart->Parent = NULL;
        va_kstart->UsedNodes = 0;
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
        va_ustart->UsedNodes = 0;
        va_ustart->attributes = 0x07;  // for user
        va_ustart->virtual_base = USER_SPACE;
        va_ustart->length = (USER_END - USER_SPACE) + 1;
        va_ustart->max_length = va_ustart->length;
    }


    u64 rsz = pages << 12;
    
    va_node *entry = find_fit(USR ? va_ustart : va_kstart, rsz);
    if (entry == NULL) {
        spin_unlock(&va_lock);
        ret.status = 1;
        return ret;
    }    

    EFI_MEMORY_DESCRIPTOR alloc = vmalloc(entry->virtual_base, pages, attributes);
    if (alloc.Attribute != 0) {
        spin_unlock(&va_lock);
        ret.status = 1;
        return ret;
    }

    ret.attributes = attributes;
    ret.base = alloc.VirtualStart;
    ret.pages = pages;
    ret.status = 0;

    if ((entry->length - rsz) == 0) {
        entry->virtual_base += rsz;
        entry->length -= rsz;
        UpdateParents(entry);
        if (entry->UsedNodes == 0) {
            va_entries--;
            va_hd *pg_hd = find_hd(entry);
            pg_hd->free_entries++;

            if (entry->Parent != NULL) {
                if (entry->Parent->va_left == entry) {
                    entry->Parent->va_left = NULL;
                    entry->Parent->UsedNodes &= ~LEFT;
                } else if (entry->Parent->va_right == entry) {
                    entry->Parent->va_right = NULL;
                    entry->Parent->UsedNodes &= ~RIGHT;
                }

                entry->Parent = NULL;
            }

            if (pg_hd->free_entries == max && va_metadata_pages > 0 && va_latest == entry && pg_hd->unused_entries == 0) {
                EFI_MEMORY_DESCRIPTOR alloc = {0};
                u64 adr = (u64)((u8 *)pg_hd - 0x1000);
                va_hd *parhd = (va_hd *)adr;
                parhd->next_page = pg_hd->next_page;
                alloc.NumberOfPages = 1;
                alloc.VirtualStart = (u64)pg_hd;
                vfree(alloc);
                va_metadata_pages--;
                va_top -= 0x1000;
                u64 base = (u64)(((u8 *)parhd + sizeof(va_hd)) + (sizeof(va_node) * 
                (max - (int)parhd->free_entries - 1) < 0 ? 0 : (max - (int)parhd->free_entries - 1)));
                va_latest = (va_node *)base;
            } else {
                pg_hd->unused_entries++;
            }

        }
                    
    } else {
        entry->virtual_base += rsz;
        entry->length -= rsz;
        UpdateParents(entry);
    }
    

    spin_unlock(&va_lock);

    return ret;
    
}



static va_node *find_last(u16 attributes, va_hd *pghd) {
    while (pghd != NULL) {
        int entries = ((max + (int)pghd->unused_entries) - (int)pghd->free_entries);
        va_node *src = (va_node *)((u8 *)pghd + sizeof(va_hd));
        for (int i = 0; i < entries; i++) { 
            if (src->attributes == attributes && ((src->UsedNodes & 0x03) != 3)) {
                return src;
            }

            src++;
        }

        pghd = pghd->next_page;
    }

    return NULL;
}



static inline void attach_node(va_node *Parent, va_node *Child) {
    if (Parent == NULL || Child == NULL)
        return;


    if ((Parent->UsedNodes & 0x03) == 3)
        return;

    
    if (!(Parent->UsedNodes & RIGHT)) {
        Parent->va_right = Child;
        Parent->UsedNodes |= RIGHT;
    } else if (!(Parent->UsedNodes & LEFT)) {
        Parent->va_left = Child;
        Parent->UsedNodes |= LEFT;
    }

    Child->Parent = Parent;

}



void va_free(va_ret desc) {
    EFI_MEMORY_DESCRIPTOR alloc = {0};
    if (desc.status != 0 || desc.pages == 0)
        return;

    alloc.VirtualStart = desc.base;
    alloc.NumberOfPages = desc.pages;
    spin_lock(&va_lock);


    va_hd *pghd = va_header;
    va_node *entry = NULL;
    va_node *last = find_last(desc.attributes, pghd);

    if (last == NULL) {
        spin_unlock(&va_lock);
        return;
    }
    
    if (va_metadata_pages == 0 && !pghd->unused_entries)
        goto other_route;
    
    while (pghd != NULL) {
        if (!pghd->unused_entries)
            goto skip;

        int entries = ((max + (int)pghd->unused_entries) - (int)pghd->free_entries);
        va_node *src = (va_node *)((u8 *)pghd + sizeof(va_hd));
        for (int i = 0; i < entries; i++) {
            if (src->length == 0 && src->Parent == NULL && ((src->UsedNodes & 0x03) != 3)) {
                src->virtual_base = desc.base;
                src->attributes = desc.attributes;
                src->length = desc.pages << 12;
                attach_node(last, src);
                UpdateParents(src);
                pghd->unused_entries--;
                vfree(alloc);
                spin_unlock(&va_lock);
                return;
            }


            src++;
        }

        skip:

        pghd = pghd->next_page;
    }

    other_route:

    va_node *Node = find_new_entry(NoParent, NULL);
    Node->attributes = desc.attributes;
    Node->virtual_base = desc.base;
    Node->length = desc.pages << 12;
    attach_node(last, Node);
    UpdateParents(Node);

    vfree(alloc);
    spin_unlock(&va_lock);
}
