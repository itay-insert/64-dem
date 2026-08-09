#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "paging.h"



u64 calculate_pages(EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize) {
    u64 pages = 0;
    for (u64 i = 0; i < (memory_map_size / DescriptorSize); i++) {
        u8 *ptr = (u8 *)memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        if (desc->Type == EfiConventionalMemory) {
            pages += desc->NumberOfPages;
        }
    }
    return pages;
}


int memcmp(const void *ptr1, const void *ptr2, size_t count) {
    const uint8_t *a = (const uint8_t*)ptr1;
    const uint8_t *b = (const uint8_t*)ptr2;

    for (size_t i = 0; i < count; i++)
    {
        if (a[i] != b[i])
        {
            return a[i] - b[i];
        }
    }

    return 0;
}

void *memset(void *ptr, int value, size_t num) {
    unsigned char val = (unsigned char)value;
    unsigned char *dest = ptr;
    if (num == 0) {
        ptr = NULL;
        return ptr;
    }

    for (size_t i = 0; i < num; i++) {
        dest[i] = val;
    }
    return ptr;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;

    
    return dest;
}

u8 check_byte(u8 byte, u8 bit, u8 dir) {
    if (dir == 1) {
        byte = byte >> (7 - bit);
    }  else {
        byte = byte >> bit;
    }
    return (byte & 0x01);
}

u8 set_bit(u8 byte, u8 bit, u8 value, u8 dir) {
    u8 mask;

    if (dir == 1) {
        mask = 1 << (7 - bit); // if dir=1, start leftmost
    } else {
        mask = 1 << bit;   // else: start rightmost
    }

    if (value)
        byte |= mask; // an OR operation to set the bit
    else
        byte &= ~mask; // an AND operation with the inverted mask to clear the bit
     
    return byte;    
}

u64 bitmap_base = 0;
u64 bitmapSize = 0;


void allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size) {
    bitmap_base = (u64)bitmap;
    bitmapSize = bitmap_size;
    for (u64 i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xff;
    }
    u64 count;
    for (u64 i = 0; i < (memory_map_size / DescriptorSize); i++) {
        u8 *ptr = (u8 *)memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        count = desc->PhysicalStart >> 12;
        u64 PageCount = desc->NumberOfPages;
        if (desc->Type == EfiConventionalMemory && desc->PhysicalStart >= 0x100000) {
            if ((count & 7) > 0) {
                u64 bits_left = 8 - (count & 7);
                if (bits_left <= PageCount) {
                    bitmap[count>>3] &= (u8)~(0xff >> (count & 7));
                    PageCount -= bits_left;
                    count += bits_left;
                } else {
                    for (u64 i = 0; i < PageCount; i++) {
                        bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 0x7, 0, 1);
                        count++;
                    }
                    PageCount = 0;
                }
            }
            u64 eightPage_count = PageCount >> 3;
            u64 onePage_count = PageCount & 7;
            for (u64 j = 0; j < eightPage_count; j++) {
                bitmap[count>>3] = 0;
                count += 8;
            }
            bitmap[count>>3] &= (u8)~(0xff << (8 - onePage_count));
        } else {
            if ((count & 7) > 0) {
                u64 bits_left = 8 - (count & 7);
               if (bits_left <= PageCount) {
                    bitmap[count>>3] |= (u8)(0xff >> (count & 7));
                    PageCount -= bits_left;
                    count += bits_left;
                } else {
                    for (u64 i = 0; i < PageCount; i++) {
                        bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 0x7, 1, 1);
                        count++;
                    }
                    PageCount = 0;
               }
            
            }
            u64 eightPage_count = PageCount >> 3;
            u64 onePage_count = PageCount & 7;
            for (u64 j = 0; j < eightPage_count; j++) {
                bitmap[count>>3] = 0xff;
                count += 8;
            }
            bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));
        } 
    }
    u64 PageCount = (kernel_end - kernel_start + 4095) >> 12;
    count = kernel_start >> 12;
    if ((count & 7) > 0) {
        u64 bits_left = 8 - (count & 7);
        if (bits_left <= PageCount) {
            bitmap[count>>3] |= (u8)(0xff >> (count & 7));
            PageCount -= bits_left;
            count += bits_left;
        } else {
            for (u64 i = 0; i < PageCount; i++) {
                bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 0x7, 1, 1);
                count++;
            }
            PageCount = 0;
        }
        
    }
    u64 eightPage_count = PageCount >> 3;
    u64 onePage_count = PageCount & 7;
    for (u64 j = 0; j < eightPage_count; j++) {
        bitmap[count>>3] = 0xff;
        count += 8;
    }
    bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));


}

u64 former_count = 0;

EFI_MEMORY_DESCRIPTOR alloc_frame(u64 PageCount) {
    EFI_MEMORY_DESCRIPTOR ret = {0};
    u8 *bitmap = (u8 *)bitmap_base;
    if (PageCount == 0) {
        ret.Attribute = 2; // 0 = success, 1 = error: not enough memory, 2 = error: invalid parameter
        return ret;
    }
    u64 count = former_count;
    u64 count_tar = count;
    u64 match_count = 0;
    while (match_count < PageCount) {
        if ((count>>3) >= bitmapSize) {
            ret.Attribute = 1;
            return ret;
        } 
        if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 0) {
            match_count++;
            count++;
        } else if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) {
            match_count = 0;
            while (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) {
                count++;
                if ((count>>3) >= bitmapSize) {
                    ret.Attribute = 1;
                    return ret;
                }
            }
            count_tar = count;
        } 
    }
    former_count = count;
    ret.PhysicalStart = count_tar<<12;
    ret.VirtualStart = count_tar<<12;
    ret.NumberOfPages = PageCount;
    ret.Attribute = 0;
    count = count_tar;

    if ((count & 7) > 0) {
        u64 bits_left = 8 - (count & 7);
        if (bits_left <= PageCount) {
            bitmap[count>>3] |= (u8)(0xff >> (count & 7));
            PageCount -= bits_left;
            count += bits_left;
        } else {
            for (u64 i = 0; i < PageCount; i++) {
                bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 7, 1, 1);
                count++;
            }
            PageCount = 0;
        }
        
    }
    u64 eightPage_count = PageCount >> 3;
    u64 onePage_count = PageCount & 7;
    for (u64 j = 0; j < eightPage_count; j++) {
        bitmap[count>>3] = 0xff;
        count += 8;
    }
    bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));

    return ret;
}



void free_frame(EFI_MEMORY_DESCRIPTOR frame) {
    u8 *bitmap = (u8 *)bitmap_base;
    u64 count = frame.PhysicalStart >> 12;
    u64 PageCount = frame.NumberOfPages;
    if (count < former_count) {
        former_count = count;
    }
    if ((count & 7) > 0) {
        u64 bits_left = 8 - (count & 7);
        if (bits_left <= PageCount) {
            bitmap[count>>3] &= (u8)~(0xff >> (count & 7));
            PageCount -= bits_left;
            count += bits_left;
        } else {
            for (u64 i = 0; i < PageCount; i++) {
                bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 0x7, 0, 1);
                count++;
            }
            PageCount = 0;
        }
    }
    u64 eightPage_count = PageCount >> 3;
    u64 onePage_count = PageCount & 7;
    for (u64 j = 0; j < eightPage_count; j++) {
        bitmap[count>>3] = 0;
        count += 8;
    }
    bitmap[count>>3] &= (u8)~(0xff << (8 - onePage_count));

}


void SetBitmapBase(u8 *bitmap) {
    bitmap_base = (u64)bitmap;
}

void fill_bitmap(u64 count, u64 PageCount, u8 *bitmap) {
    if ((count & 7) > 0) {
        u64 bits_left = 8 - (count & 7);
        if (bits_left <= PageCount) {
            bitmap[count>>3] |= (u8)(0xff >> (count & 7));
            PageCount -= bits_left;
            count += bits_left;
        } else {
            for (u64 i = 0; i < PageCount; i++) {
                bitmap[count>>3] = set_bit(bitmap[count>>3], (u8)count & 7, 1, 1);
                count++;
            }
            PageCount = 0;
        }
        
    }
    u64 eightPage_count = PageCount >> 3;
    u64 onePage_count = PageCount & 7;
    for (u64 j = 0; j < eightPage_count; j++) {
        bitmap[count>>3] = 0xff;
        count += 8;
    }
    bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));

}

int run_simulation(u64 count, u64 pages, u8 *bitmap) {
    int status = 0;
    if (pages == 0) {
        status = 2; // 0 = wouldn't cross bounds, 1 = would cross bounds, 2 = invalid parameter
        return status;
    }
    while (pages > 0) {
        if ((count>>3) >= bitmapSize) {
            status = 1;
            return status;
        }
        if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 0) pages--;
        count++;
    }
    return status;
}

EFI_MEMORY_DESCRIPTOR kmalloc(u64 virtual_address, u64 pages) {
    EFI_MEMORY_DESCRIPTOR ret = {0};
    u8 *bitmap = (u8 *)bitmap_base;
    u64 *PML4 = (u64 *)KernelPML4;
    if (pages == 0) {
        ret.Attribute = 2;
        return ret;
    }
    u64 count = former_count;
    u64 count_tar = count;
    u64 count_virt = virtual_address;
    u64 match_count = 0;
    u64 PageCount = pages;
    if (run_simulation(count, PageCount, bitmap) == 1) {
        ret.Attribute = 1;
        return ret;
    }
    while (PageCount > 0) {
        if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 0) {
            match_count++;
            count++;
            PageCount--;
        } else if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) {
            fill_bitmap(count_tar, match_count, bitmap);
            create_mapping(count_virt, count_tar<<12, match_count, 0x03, PML4);
            count_virt += (match_count << 12);
            match_count = 0;
            while (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) count++;
            count_tar = count;
        }
    }
    fill_bitmap(count_tar, match_count, bitmap);
    create_mapping(count_virt, count_tar<<12, match_count, 0x03, PML4);
    PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(virtual_address, PML4);
    ret.PhysicalStart = lookup.physical_address;
    ret.VirtualStart = virtual_address;
    ret.Attribute = 0;
    ret.NumberOfPages = pages;
    flush_pages(virtual_address, pages);
    former_count = count;
    return ret;
}

void kfree(EFI_MEMORY_DESCRIPTOR allocation) {
    u64 *PML4 = (u64 *)KernelPML4;
    while (allocation.NumberOfPages > 0) {
        EFI_MEMORY_DESCRIPTOR frame = {0};
        frame.VirtualStart = allocation.VirtualStart;
        PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(allocation.VirtualStart, PML4);
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
}

#define Free 0
#define Used 1
#define Header 2
#define NoEntriesLeft 0


typedef struct dma_entry dma_entry;

struct dma_entry {
    int status;
    u64 SizeInPages;
    dma_entry *next_entry;
} __attribute__((packed));

typedef struct {
    char sign[8];
    int status;
    u64 SizeInPages;
    dma_entry *home_entry;
} __attribute__((packed)) dma_descriptor;

int entries = 0;

dma_entry *dma_header = NULL;
dma_entry *dma_start = NULL;

dma_entry *dma_latest = NULL;

int limit = 0;
int metadata_pages = 0;

u64 dma_top = DMA_POOL;

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

dma_entry *find_header(dma_entry *entry) {
    while (entry->status != Header) {
        entry = (dma_entry *)((u8 *)entry - sizeof(dma_entry));
    } 
        
    return entry;
}

u64 find_descriptorBase(u64 Base) {
    while (1) {
        dma_descriptor *ptr = (dma_descriptor *)Base;
        if (memcmp(ptr->sign, "DMA_POOL", 8) == 0) 
            return Base;
        Base++;
    }
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
