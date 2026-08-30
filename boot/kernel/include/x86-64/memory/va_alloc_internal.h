#ifndef VA_ALLOC_INTERNAL_H
#define VA_ALLOC_INTERNAL_H

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

typedef struct header hp_header;

struct header {
    u64 free_entries;
    hp_header *next_page;
} __attribute__((packed));

typedef struct heap_entry heap_entry;

struct heap_entry {
    u64 virtual_base;
    u64 pages;
    u64 max_pages;
    heap_entry *right;
    heap_entry  *left;
} __attribute__((packed));


extern int entries;
extern heap_entry *heap_header;
extern heap_entry *heap_start;
extern heap_entry *heap_latest;
extern int limit;
extern int metadata_pages;
extern u64 heap_top;
extern spinlock_t heap_lock;


heap_entry *find_new_entry(void);

KERNEL_EXTERN_C_END

#endif