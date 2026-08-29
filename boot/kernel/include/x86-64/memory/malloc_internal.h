#ifndef MALLOC_INTERNAL_H
#define MALLOC_INTERNAL_H

#include <stddef.h>
#include "x86-64/paging.h"
#include "c_compat.h"
#include "x86-64/spinlock.h"

KERNEL_EXTERN_C_BEGIN

enum HeapStatus {
    Free = 0,
    Used = 1,
    Header = 2,
};


#define NoEntriesLeft 0


typedef struct heap_entry heap_entry;

struct heap_entry {
    int status;
    u64 SizeInBytes;
    u64 virtual_base;
    u16 attributes;
    heap_entry *next_entry;
} __attribute__((packed));


extern int entries;

extern heap_entry *heap_header;
extern heap_entry *heap_start;

extern heap_entry *heap_latest;

extern int limit;
extern int metadata_pages;

extern u64 heap_top;

extern u64 free_kbase;

extern spinlock_t heap_lock;

KERNEL_EXTERN_C_END

#endif