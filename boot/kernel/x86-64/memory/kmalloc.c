#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/va_alloc.h"

typedef struct slabobj slabobj;

struct slabobj {
    u64 PageBase;
    int FreeObs;
    u8 obs[64];
    slabobj *next_object;
} __attribute__((packed));

typedef struct {
    u64 Base;
    u64 Size;
    u16 attributes;
    slabobj *slabOrg;
} __attribute__((packed)) k_header;


typedef struct slabhd slabhd;

struct slabhd {
    u64 free_entries;
    u64 unused_entries;
    slabhd *next_page;
} __attribute__((packed));


slabobj *start = NULL;
slabobj *latest = NULL;

slabhd *shd = NULL;

u64 slab_pages = 0;

u64 max = 0;

spin_lock_t klock = {0};


static inline slabhd *find_hd(slabobj *slab) {
    slabhd *hd = (slabhd *)((u64)slab & ~0xFFF);
    return hd;
}

static inline slabobj *createSlab() {
    if (start == NULL) {
        start = (slabobj *)((u8 *)shd + sizeof(slabhd));
        memset(start, 0, sizeof(slabobj));
        start->next_object = NULL;
        latest = start;
        return start;
    }

    slabhd *hd = find_hd(latest);
    if (hd->free_entries == 0) {
        va_ret alloc = va_alloc(1, 0x03);
        if (alloc.status != 0)
            return NULL;

        slabhd *new_hd = (slabhd *)alloc.base;
        new_hd->free_entries = max;
        new_hd->unused_entries = 0;
        new_hd->next_page = NULL;
        hd->next_page = new_hd;
        latest = (slabobj *)((u8 *)new_hd + sizeof(slabhd));
        
    }

} 


void *kmalloc(u64 Size) {
    Size = Size + sizeof(k_header);
    spin_lock(&klock);


    if (shd == NULL) {
        va_ret alloc = va_alloc(1, 0x03);
        if (alloc.status != 0) {
            spin_unlock(&klock);
            return NULL;
        }
        shd = (slabhd *)alloc.base;
        shd->unused_entries = 0;
        shd->free_entries = (4096 - sizeof(slabhd)) / sizeof(slabobj);
        max = shd->free_entries;
        shd->next_page = NULL;
    }


    if (Size < 4096) {

    }
}