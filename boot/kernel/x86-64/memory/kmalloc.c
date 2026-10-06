 
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
    u16 _pad;
    int sc;
    int index;
    slabobj *slabOrg;
} k_header;


typedef struct slabhd slabhd;
typedef struct slab_cache slab_cache;

struct slabhd {
    u64 free_entries;
    u64 unused_entries;
    slabhd *next_page;
    slabhd *former_page;
    slab_cache *used_cache;
    slab_cache *unused_cache;
} __attribute__((packed));

struct slab_cache {
    u64 addresses[(4096 - sizeof(slabhd)) / sizeof(slabobj)];
    int index;
} __attribute__((packed));


#define Used2048 0xFFFFFFFFFFFFFFFFULL
#define Used1024 0xFFFFFFFF

#define INVALID_BASE 1

typedef enum {
    NO_ERROR = 0,
    SIZE_TOO_MUCH = 1,
    UNKNOWN = 2,
} ERROR_TYPES;

typedef struct {
    int sc;
    void *addr;
} search_ret;

slabobj *start = NULL;
slabobj *latest = NULL;

slabhd *shd = NULL;

u64 slab_pages = 0;

u64 kmax = 0;

spinlock_t klock = {0};


static inline slabhd *find_hd(slabobj *slab) {
    slabhd *hd = (slabhd *)((u64)slab & ~0xFFF);
    return hd;
}

static inline slabobj *createSlab(void) {
    if (start == NULL) {
        start = (slabobj *)((u8 *)shd + sizeof(slabhd));
        memset(start, 0, sizeof(slabobj));
        start->next_object = NULL;
        shd->free_entries--;
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
        new_hd->former_page = hd;
        alloc = va_alloc(1, 0x03);
        if (alloc.status != 0)
            return NULL;
        new_hd->unused_cache = (slab_cache *)alloc.base;
        new_hd->unused_cache->index = 0;
        alloc = va_alloc(1, 0x03);
        if (alloc.status != 0)
            return NULL;
        new_hd->used_cache = (slab_cache *)alloc.base;
        new_hd->used_cache->index = 0;
        
        slab_pages++;
        latest->next_object = (slabobj *)((u8 *)new_hd + sizeof(slabhd));
        latest = latest->next_object;
        latest->PageBase = INVALID_BASE;
        latest->next_object = NULL;
        new_hd->free_entries--;

    } else {
        hd = shd;
        int index = hd->unused_cache->index;
        u64 *ptr = hd->unused_cache->addresses;
        while (index == 0) {
           if (hd->next_page == NULL) break;
           hd = hd->next_page;
           index = hd->unused_cache->index;
           ptr = hd->unused_cache->addresses;
        }
        
        if (index != 0) {
           index--;
           hd->unused_entries--;
           return (slabobj *)ptr[index];
        }
           

        latest->next_object = (slabobj *)((u8 *)latest + sizeof(slabobj));
        latest = latest->next_object;
        latest->PageBase = INVALID_BASE;
        latest->next_object = NULL;
        hd = find_hd(latest);
        hd->free_entries--;

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

static inline u64 clean64(u64 Long, u8 ind) {
    if (ind > 63)
        return Long;

    return (Long & ~(1ULL << ind));
}



static inline void set128(u64 *low, u64 *high, u8 ind) {
    if (ind > 127)
        return;
    *low = set64(*low, ind);
    *high = ((ind >> 6) ? set64(*high, (ind - 64)) : *high);
}

static inline void clean128(u64 *low, u64 *high, u8 ind) {
    if (ind > 127)
        return;
    *low = clean64(*low, ind);
    *high = ((ind >> 6) ? clean64(*high, (ind - 64)) : *high);
}



static inline search_ret find_objs(int req, u64 *buff, u64 base, bool creq) {
    search_ret ret = {0};
    if (req == 0) {
       ret.addr = NULL;
       return ret;
    }
    if (!creq && !(!buff[0] && !buff[1])) {
       ret.addr = NULL;
       return ret;
    }
    int sc = -1;
    u64 low = *buff;
    u64 high = buff[1];
    int ind = 0;
    while ((ind = Bsf128(~low, ~high)) < 128) {
        mask128(&low, &high, ind);
        int ind2 = Bsf128(low, high);
        if ((ind2 - ind) >= req) {
            sc = ind;
            break;
        }
        Or128(&low, &high, ind2);
    }

    if (sc == -1) {
       ret.addr = NULL;
       return ret;
    }

    ret.sc = sc;

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

    ret.addr = (void *)addr;
    return ret;

}

static inline void clean_cache(int req, u64 *buff, int sc) {

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
                  map[i] = 0ULL;

               sc += qwc << 6;
               req -= qwc << 6;
           } else if (dwc > 0) {
               u32 *map = (u32 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < dwc; i++) 
                  map[i] = 0ULL;

               sc += dwc << 5;
               req -= dwc << 5;
           } else if (wc > 0) {
               u16 *map = (u16 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < wc; i++) 
                  map[i] = 0x0000;

               sc += wc << 4;
               req -= wc << 4;
           } else if (bc > 0) {
               u8 *map = (u8 *)((u8 *)buff + (sc >> 3));
               for (int i = 0; i < bc; i++) 
                  map[i] = 0x00;

               sc += bc << 3;
               req -= bc << 3;
           } else if (c > 0) {
               for (int i = sc; i < (sc+c); i++) clean128(&buff[0], &buff[1], i);
               sc += c;
               req -= c;
           }
       }

    } else if (req >> 3) {
        while (req > 0) {
           clean128(&buff[0], &buff[1], sc);
           sc++;
           req--;
           if (!(sc & 7))
              goto recheck;
        }
    } else {
        for (int i = sc; i < (sc+req); i++) 
            clean128(&buff[0], &buff[1], i);
    }


}





static inline bool check_page(slabhd *hd) {
    u64 used = max - hd->free_entries - hd->unused_entries;

    if (used == 0)
        return true;
    else return false;

}




static inline slabhd *deallocate_slab(slabhd *hd) {
    slabhd *former_page = hd->former_page;

    u64 unused_cache = (u64)hd->unused_cache;
    u64 used_cache = (u64)hd->used_cache;
    va_ret desc = {0};
    desc.base = (u64)hd;
    desc.attributes = 0x03;
    desc.pages = 1;
    va_free(desc);

    desc.base = unused_cache;
    va_free(desc);

    desc.base = used_cache;
    va_free(desc);

    return former_page;
}



static inline slabobj *reset_latest(slabobj *last, slabhd *last_hd) {
    u64 place = kmax - last_hd->free_entries;
    last = (slabobj *)((u8 *)last_hd + sizeof(slabhd));
    last = &last[place-1];
    last->next_object = NULL;
    return last;
}



static inline void link_pages(slabhd *former, slabhd *next) {
    u64 place = kmax - former->free_entries;
    slabobj *last_slab = (slabobj *)((u8 *)former + sizeof(slabhd));
    last_slab = &last_slab[place-1];
    last_slab->next_object = (slabobj *)((u8 *)next + sizeof(slabhd));

}


static inline int get_index(slabobj *slab) {
    u64 addr = (u64)slab;
    addr = addr & 0xfff;
    return (int)((addr - sizeof(slabhd)) / sizeof(slabobj));
}

void *kmalloc(u64 Size) {
    u64 addr = 0;
    u64 save_size = 0;
    bool conreq = true;
    bool back = false;
    Size = Size + sizeof(k_header);
    spin_lock(&klock);


    if (shd == NULL) {
        va_ret alloc = va_alloc(1, 0x03);
        if (alloc.status != 0) {
            spin_unlock(&klock);
            return NULL;
        }
        memset((void *)alloc.base, 0, 4096);
        shd = (slabhd *)alloc.base;
        shd->unused_entries = 0;
        shd->free_entries = (4096 - sizeof(slabhd)) / sizeof(slabobj);
        kmax = shd->free_entries;
        shd->next_page = NULL;
        shd->former_page = NULL;
        alloc = va_alloc(1, 0x03);
        if (alloc.status != 0) {
           spin_unlock(&klock);
           return NULL;
        }
        shd->unused_cache = (slab_cache *)alloc.base;
        shd->unused_cache->index = 0;
        alloc = va_alloc(1, 0x03);
        if (alloc.status != 0) {
           spin_unlock(&klock);
           return NULL;
        }
        shd->used_cache = (slab_cache *)alloc.base;
        shd->used_cache->index = 0;
    }

    slabobj *slab = NULL;
    void *place = NULL;
    slabhd *hd = NULL;
    search_ret obj_ret = {0};
    int req;
    int slab_pid;
    if (Size < 4096) {
        find_slab:

        if (start == NULL) {
            slab = createSlab();
            if (slab == NULL) {
                spin_unlock(&klock);
                return NULL;
            }

            memset(slab, 0, sizeof(slabobj));
            slab->next_object = NULL;
            slab->PageBase = INVALID_BASE;
            slab->FreeObs = 128;
            shd->unused_entries++;
            shd->unused_cache->addresses[shd->unused_cache->index] = (u64)slab;
            if (shd->unused_cache->index < max) shd->unused_cache->index++;
            start = slab;
        }

        req = (int)(Size + 31) >> 5;
       if (conreq) {
            hd = shd;
            while (hd != NULL) {
                u64 *ptr = hd->used_cache->addresses;
                int index = hd->used_cache->index;
                for (int i = 0; i < index; i++) {
                   if (ptr[i] != INVALID_BASE) {
                       slab = (slabobj *)ptr[i];
                       if ((slab->FreeObs - req) >= 0) {
                          obj_ret = find_objs(req, slab->Cache_2048, slab->PageBase, conreq);
                          if (obj_ret.addr == NULL) goto failed;
                          place = obj_ret.addr;
                          slab->FreeObs -= req;
                          slab_pid = i;
                          goto exit;
                       }
                   }
                   failed:
               }
               hd = hd->next_page;
           }
        }

        exit:
        if (place != NULL) goto skip;
        hd = shd;
        while (hd != NULL) {
            if (hd->unused_cache->index != 0) {
                hd->unused_cache->index--;
                slab = (slabobj *)hd->unused_cache->addresses[hd->unused_cache->index];
                hd->unused_entries--;
                obj_ret = find_objs(req, slab->Cache_2048, slab->PageBase, conreq);
                place = obj_ret.addr;
                slab->FreeObs -= req;
                int index = get_index(slab);
                slab_pid = index;
                hd->used_cache->addresses[index] = (u64)slab;
                if (index >= hd->used_cache->index)
                   hd->used_cache->index = index + 1;
                break;
            }

           hd = hd->next_page;
        }

        
        skip:
                          

        if (place == NULL) {
            slab = createSlab();
            if (slab == NULL) {
                spin_unlock(&klock);
                return NULL;
            }
            memset(slab->Cache_2048, 0, 16);
            slab->FreeObs = 128;
            hd = find_hd(slab);
            hd->unused_entries++;
            req = (int)(Size + 31) >> 5;
            obj_ret = find_objs(req, slab->Cache_2048, slab->PageBase, conreq);
            place = obj_ret.addr;
            slab->FreeObs -= req;
            hd->unused_entries--;
            int index = get_index(slab);
            hd->used_cache->addresses[index] = (u64)slab;
            slab_pid = index;
            if (index >= hd->used_cache->index)
                   hd->used_cache->index = index + 1;
            
        }

        if (slab->PageBase == INVALID_BASE && conreq) {
          va_ret alloc = va_alloc(1, 0x03);

          if (alloc.status != 0) {
              clean_cache(req, slab->Cache_2048,     obj_ret.sc);
              slab->FreeObs += req;

              if (slab->FreeObs == 128) {
                  hd->used_cache->addresses[get_index(slab)] = INVALID_BASE;
                  hd->unused_entries++;
                  hd->unused_cache->addresses[hd->unused_cache->index] = (u64)slab;
                  hd->unused_cache->index++;
              }

              spin_unlock(&klock);
              return NULL;
           }
           
           slab->PageBase = alloc.base;
           place = (void *)((u64)place - 1 + alloc.base);
        }
        if (back) 
            goto home;


    } else if (!(Size & 0xFFFULL)) {
        va_ret alloc = va_alloc((Size>>12), 0x03);
        if (alloc.status != 0) {
            spin_unlock(&klock);
            return NULL;
        }
        addr = alloc.base;
        place = (void *)addr;
    } else {
        va_ret alloc = va_alloc(((Size+4095)>>12), 0x03);
        if (alloc.status != 0) {
            spin_unlock(&klock);
            return NULL;
        }
        addr = alloc.base;
        save_size = Size;
        Size = Size & 0xFFFULL;
        back = true;
        conreq = false;
        goto find_slab;  
        home:

        place = (void *)addr;
        u64 new_addr = ((addr + save_size) & ~0xFFFULL);

        slab->PageBase = new_addr;
        Size = save_size;

    }

    if (place == NULL) {
       spin_unlock(&klock);
       return NULL;
    }

    k_header *header = (k_header *)((u64)place);
    header->Base = (u64)place;
    header->Size = Size;
    header->attributes = 0x03;
    header->slabOrg = slab;
    header->sc = obj_ret.sc;
    header->index = slab_pid;

    place = (void *)((u8 *)place + sizeof(k_header));

    spin_unlock(&klock);
    return place;
}



void kfree(void *alloc) {
    spin_lock(&klock);
    bool back = false;
    k_header *header = (k_header *)((u8 *)alloc - sizeof(k_header));
    if (header->Size < 4096) {
       free_slab:
       int req = (header->Size + 31) >> 5;
       int slab_pid = header->index;
       slabobj *slab = header->slabOrg;
       clean_cache(req, slab->Cache_2048, header->sc);
       slab->FreeObs += req;
       slabhd *hd = find_hd(slab);
       if (slab->FreeObs == 128) {
           hd->unused_cache->addresses[hd->unused_cache->index] = (u64)slab;
           hd->unused_cache->index++;
           hd->used_cache->addresses[slab_pid] = INVALID_BASE;
           hd->unused_entries++;
           va_ret alloc = {0};
           alloc.base = slab->PageBase;
           alloc.pages = 1;
           alloc.attributes = 0x03;
           va_free(alloc);
           slab->PageBase = INVALID_BASE;
       }

       bool miss = false;
       slabhd *latest_hd = find_hd(latest);
       while (latest_hd != shd) {
          if (check_page(latest_hd)) {
              if (miss) {
                  latest_hd->former_page->next_page = latest_hd->next_page;
              latest_hd->next_page->former_page = latest_hd->former_page;
                  link_pages(latest_hd->former_page, latest_hd->next_page);
              } else if (!miss) 
                  latest_hd->former_page->next_page = NULL;
              latest_hd = deallocate_slab(latest_hd);
              slab_pages--;
              if (!miss)
                  latest = reset_latest(latest, latest_hd);
          } else {
              latest_hd = latest_hd->former_page;
              miss = true;
          }
      }

      if (back)
         goto home;

    } else if (!(header->Size & 0xFFFULL)) {
        free_aligned:
        va_ret desc = {0};
        desc.base = header->Base;
        desc.attributes = 0x03;
        desc.pages = header->Size >> 12;
        va_free(desc);
    } else {
        u64 size = header->Size;
        header->Size = header->Size & 0xFFFULL;
        back = true;
        goto free_slab;
        home:
        header->Size = size;
        goto free_aligned;
   }

   spin_unlock(&klock);

}
