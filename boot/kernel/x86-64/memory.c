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

typedef struct {
    u64 code;
    u64 descriptorEntry;
    EFI_MEMORY_DESCRIPTOR *memory_map;
    EFI_PHYSICAL_ADDRESS physical_start;
    u32 Type;
    u64 Pagenum;
    u64 count;
} alloc_debug;


alloc_debug allocator_init(u8 *bitmap, EFI_MEMORY_DESCRIPTOR *memory_map, u64 memory_map_size, u64 DescriptorSize, u64 kernel_start, u64 kernel_end, u64 bitmap_size) {
    alloc_debug alc = {0, DescriptorSize, memory_map, 0, 0, 0, 0};
    u64 count;
    for (u64 i = 0; i < (memory_map_size / DescriptorSize); i++) {
        u8 *ptr = (u8 *)memory_map;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + i * DescriptorSize);
        count = desc->PhysicalStart >> 12;
        u64 PageCount = desc->NumberOfPages;
        if (desc->Type == EfiConventionalMemory) {
            if ((count & 7) > 0) {
                if ((count & 7) <= PageCount) {
                    bitmap[count>>3] &= (u8)~(0xff >> (8 - (count & 7)));
                    PageCount -= count & 7;
                    count += count & 7;
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
                if ((count >> 3) >= bitmap_size) {
                    alc.code = ((u64)bitmap + (count >> 3));
                    alc.descriptorEntry = ((u64)desc - (u64)memory_map) / DescriptorSize;
                    alc.memory_map = desc;
                    alc.Type = desc->Type;
                    alc.Pagenum = desc->NumberOfPages;
                    alc.physical_start = desc->PhysicalStart;
                    alc.count = count;
                    return alc;
                }
                bitmap[count>>3] = 0;
                count += 8;
            }
            bitmap[count>>3] &= (u8)~(0xff << (8 - onePage_count));
        } else {
            if ((count & 7) > 0) {
               if ((count & 7) <= PageCount) {
                    bitmap[count>>3] |= (u8)(0xff >> (8 - (count & 7)));
                    PageCount -= count & 7;
                    count += count & 7;
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
                if ((count >> 3) >= bitmap_size) {
                    alc.code = ((u64)bitmap + (count >> 3));
                    alc.descriptorEntry = ((u64)desc - (u64)memory_map) / DescriptorSize;
                    alc.memory_map = desc;
                    alc.Type = desc->Type;
                    alc.Pagenum = desc->NumberOfPages;
                    alc.physical_start = desc->PhysicalStart;
                    alc.count = count;
                    return alc;
                }
                bitmap[count>>3] = 0xff;
                count += 8;
            }
            bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));
        } 
    }
    u64 PageCount = (kernel_end - kernel_start + 4095) >> 12;
    count = kernel_start >> 12;
    if ((count & 7) > 0) {
        if ((count & 7) <= PageCount) {
            bitmap[count>>3] |= (u8)(0xff >> (8 - (count & 7)));
            PageCount -= count & 7;
            count += count & 7;
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
        if ((count >> 3) >= bitmap_size) {
            alc.code = ((u64)bitmap + (count >> 3));
            alc.descriptorEntry = 0;
            alc.memory_map = 0;
            alc.Type = 0;
            alc.Pagenum = 0;
            alc.physical_start = 0;
            alc.count = count;
            return alc;
        }
        bitmap[count>>3] = 0xff;
        count += 8;
    }
    bitmap[count>>3] |= (u8)(0xff << (8 - onePage_count));

    return alc;
}