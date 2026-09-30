#ifndef VA_ALLOC_INTERNAL_H
#define VA_ALLOC_INTERNAL_H

#include <stddef.h>
#include "x86-64/paging.h"
#include "c_compat.h"
#include "x86-64/spinlock.h"

KERNEL_EXTERN_C_BEGIN


#define NoEntriesLeft 0

typedef struct header va_hd;

struct header {
    u64 free_entries;
    u64 unused_entries;
    va_hd *next_page;
} __attribute__((packed));

typedef struct va_node va_node;

struct va_node {
    u64 virtual_base;
    u64 length;
    u64 max_length;
    u16 attributes;
    u8 UsedNodes;
    va_node *va_right;
    va_node  *va_left;
    va_node *Parent;
} __attribute__((packed));


typedef struct {
    int status;
    u64 base;
    u64 pages;
    u16 attributes;
} va_ret;

extern int va_entries;
extern int max;
extern va_hd *va_header;
extern va_node *va_kstart;
extern va_node *va_ustart;
extern va_node *va_latest;
extern int va_limit;
extern int va_metadata_pages;
extern u64 va_top;
extern spinlock_t va_lock;


KERNEL_EXTERN_C_END

#endif
