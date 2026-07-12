#include <stddef.h>
#include <stdint.h>
#include "efi_memory_types.h"
#include "uint_definitions.h"


typedef unsigned int UINT32;
typedef unsigned long long UINT64; 


typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;


typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;


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


void allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size) {
    bitmap_base = (u64)bitmap;
    for (u64 i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xff;
    }
    u64 count;
    for (u64 i = 0; i < (memory_map_size / DescriptorSize); i++) {
        u8 *ptr = (u8 *)memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        count = desc->PhysicalStart >> 12;
        u64 PageCount = desc->NumberOfPages;
        if (desc->Type == EfiConventionalMemory) {
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


EFI_MEMORY_DESCRIPTOR alloc_frame(u64 PageCount) {
    u8 *bitmap = (u8 *)bitmap_base;
    u64 count = 0;
    u64 count_tar = 0;
    u64 match_count = 0;
    while (match_count < PageCount) {
        if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 0) {
            match_count++;
            count++;
        } else if (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) {
            match_count = 0;
            while (check_byte(bitmap[count>>3], (u8)count & 0x7, 1) == 1) 
                count++;
            count_tar = count;
        } 
    }
    EFI_MEMORY_DESCRIPTOR ret = {0, count_tar<<12, count_tar<<12, PageCount, 0};
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