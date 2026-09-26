#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/va_alloc.h"
#include "x86-64/lowlevel.h"

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
#define Used1024 0xFFFFFFFF


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

static inline int Bsf128(u64 low, u64 high) {
    int ind = (int)Bsf(low);
    if (ind == 64) 
        return (ind + (int)Bsf(high)); // Bsf returns 64 if no 1 bit is detected
    return ind;
}


static inline void mask128(u64 *low, u64 *high, int ind) {
    if (ind == 128) {
        *low &= ~Used2048;
        *high &= ~Used2048;
        return;
    }
    *low = *low & ((ind >> 6) ? ~Used2048 : (Used2048 << (ind & 63)));
    *high = *high & (Used2048 << ((ind >> 6) ? (ind - 64) : 0));
}

static inline void Or128(u64 *low, u64 *high, int ind) {
    if (ind == 128) {
        *low = Used2048;
        *high = Used2048;
        return;
    }
    *low = *low | ((ind >> 6) ? Used2048 : (Used2048 >> (64 - (ind & 63))));
    *high = *high | ((ind >> 6) ? ind & 63 ? (Used2048 >> (64 - (ind - 64))) : 0 : 0);
}

static inline u64 set64(u64 Long, u8 ind) {
    if (ind > 63)
        return Long;

    return (Long | (1ULL << ind));
}

static inline void set128(u64 *low, u64 *high, u8 ind) {
    if (ind > 127)
        return;
    *low = set64(*low, ind);
    *high = ((ind >> 6) ? set64(*high, (ind - 64)) : *high);
}

static inline void *find_objs(int req, u64 *buff, u64 base) {
    if (req == 0) return NULL;
    int sc = -1;
    int zc = 0;
    u64 low = *buff;
    u64 high = buff[1];
    while (Bsf128(~low, ~high) < 128) {
        int ind = Bsf128(~low, ~high);
        mask128(&low, &high, ind);
        int ind2 = Bsf128(low, high);
        if ((ind2 - ind) >= req) {
            sc = ind;
            break;
        }
        Or128(&low, &high, ind2);
    }

    if (sc == -1) 
        return NULL;
 
    u64 addr = (base + (sc << 5));

    recheck:
    
    if (!(sc & 7)) {
        while (req > 0) {
           int qwc = (req >> 6);
           int dwc = (req >> 5);
           int wc = (req >> 4);
           int bc = (req >> 3);
           int c = req & 7;
           if (qwc > 0) {
               u64 *map = (u64 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < qwc; i++) 
                  map[i] = Used2048;

               sc += qwc << 6;
               req -= qwc << 6;
           } else if (dwc > 0) {
               u32 *map = (u32 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < dwc; i++) 
                  map[i] = Used1024;

               sc += dwc << 5;
               req -= dwc << 5;
           } else if (wc > 0) {
               u16 *map = (u16 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < wc; i++) 
                  map[i] = 0xFFFF;

               sc += wc << 4;
               req -= wc << 4;
           } else if (bc > 0) {
               u8 *map = (u8 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < bc; i++) 
                  map[i] = 0xFF;

               sc += bc << 3;
               req -= bc << 3;
           } else if (c > 0) {
               for (int i = sc; i < (sc+c); i++) set128(&buff[0], &buff[1], i);
               sc += c;
               req -= c;
           }
       }

    } else if (req >> 3) {
        while (req > 0) {
           set128(&buff[0], &buff[1], sc);
           sc++;
           req--;
           if (!(sc & 7))
              goto recheck;
        }
    } else {
        for (int i = sc; i < (sc+req); i++) 
            set128(&buff[0], &buff[1], i);
    }
    
    return addr;
         
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
