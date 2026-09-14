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
    u64 Cache_2048[2];
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


#define Used2048 0xFFFFFFFFFFFFFFFFULL

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
        slab_pages++;
        latest->next_object = (slabobj *)((u8 *)new_hd + sizeof(slabhd));
        latest = latest->next_object;
        
    } else {
        slabobj *slab = start;
        while (slab != NULL) {
            if (slab->FreeObs == 128) {
                return slab;
            }

            slab = slab->next_object;
        }

        latest->next_object = (slabobj *)((u8 *)latest + sizeof(slabobj));
        latest = latest->next_object;
        
    }

    return latest;
} 


static inline void *find_objs(int req, u64 *buff, u64 base) {
         if (req == 0) return NULL;
         if (!(req & 63)) {
             req = req >> 6;
             if (buff[0] == 0) {
                 buff[0] = Used2048;
                 return (void *)base;
             } else if (buff[1] == 0) {
                 buff[1] = Used2048;
                 base = base + 2048;

             
             

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
        memset(alloc.base, 0, 4096);
        shd = (slabhd *)alloc.base;
        shd->unused_entries = 0;
        shd->free_entries = (4096 - sizeof(slabhd)) / sizeof(slabobj);
        max = shd->free_entries;
        shd->next_page = NULL;
    }


    if (Size < 4096) {
        if (start == NULL) {
            slabobj *slab = createSlab();
            if (slab == NULL) {
                spin_unlock(&klock);
                return NULL;
            }

            va_ret alloc = va_alloc(1, 0x03);
            u64 addr = alloc.base;
            memset(slab, 0, sizeof(slabobj));
            slab->PageBase = addr;
            slab->FreeObs = 128;
            slab->next_object = NULL;
        }

        slabobj *slab = start;
        void *place = NULL;
        while (slab != NULL) {
            int req = (int)(Size + 31) >> 5;
            place = find_objs(req, slab->Cache_2048, slab->PageBase);
            if (place != NULL) {
                slab->FreeObs -= req;
                break;
            }
            
            slab = slab->next_object;
        }

        if (place == NULL) {
            slab = createSlab();
            if (slab == NULL) {
                spin_unlock(&klock);
                return NULL;
            }

            va_ret alloc = va_alloc(1, 0x03);
            u64 addr = alloc.base;
            memset(slab, 0, sizeof(slabobj));
            slab->PageBase = addr;
            slab->FreeObs = 128;
            slab->next_object = NULL;

          
            
            
        }

        
    }
}