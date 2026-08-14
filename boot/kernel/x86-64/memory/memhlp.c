#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"



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
