#ifndef MALLOC_INTERNAL_H
#define MALLOC_INTERNAL_H

#include <stddef.h>
#include "x86-64/paging.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN


typedef struct heap_entry heap_entry;

struct heap_entry {
    int status;
    u64 SizeInBytes;
    u64 virtual_base;
    u16 attributes;
    heap_entry *next_entry;
} __attribute__((packed));

typedef struct {
    char sign[8];
    int status;
    u64 SizeInBytes;
    dma_entry *home_entry;
} __attribute__((packed)) malloc_descriptor;





KERNEL_EXTERN_C_END

#endif