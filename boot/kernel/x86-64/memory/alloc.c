#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"



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
