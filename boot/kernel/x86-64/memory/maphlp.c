#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/memory_mapping.h"



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
