#include <stddef.h>
#include <stdint.h>
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