#include <stddef.h>
#include <stdbool.h>
#include "x86-64/memory/memory.h"
#include "x86-64/memory/pagepool.h"


Page_Pool *pool = NULL; // pool pointer
Page_Pool *pool_start = NULL;

Page_Pool *latest_entry = NULL;
int pools_count = 0;
u64 pool_size = 0;
u64 entries = 0;
u64 max_entry_index = 0;

u64 pool_addr = 0;
u64 pool_region = 0;

void pool_init(void) {
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(1024); // allocate 4 megabytes for pool list

    pool_addr = alloc.PhysicalStart;
    pool = (Page_Pool *)alloc.PhysicalStart;
    memset(pool, 0, (1024<<12)); // zero-initialize
    pool_size = 1024 << 12;

    pool_start = pool;
    max_entry_index++;

    latest_entry = pool_start;

    entries = pool_size / sizeof(Page_Pool);

    alloc = alloc_frame(8192); // allocate 32 megabytes of memory for internal pool memory

    pool_region = alloc.PhysicalStart;
    pool->PageBase = alloc.PhysicalStart;
    pool->PageCount = 8192;

    pool->father = NULL;
    pool->Son = NULL;

    pools_count++;
}

static void delete_pool(Page_Pool *entry) {
    if (entry == NULL)
        return; // invalid entry


    if (pool_start == entry)
        pool_start = NULL;

    if (entry == latest_entry) {
        if (entry->father) 
            latest_entry = entry->father;
        else
            latest_entry = NULL;
    }


    if (entry->Son) {
        if (entry->father) {
            entry->father->Son = entry->Son;
            entry->Son->father = entry->father;
            entry->father = NULL;
        } else
            entry->Son->father = NULL;

        entry->Son = NULL;
    } else if (entry->father) {
        entry->father->Son = NULL;
        entry->father = NULL;
    }

    entry->PageCount = 0;
    entry->PageBase = 0xFFFFFFFF; // magic number, later i'll explain why
}


static Page_Pool *find_new_entry(void) {
    for (u64 i = 0; i < entries; i++) {
        if (pool[i].PageBase == 0xFFFFFFFF || i == max_entry_index) {
            if (i == max_entry_index && (max_entry_index+1) < entries)
                max_entry_index++;

            latest_entry->Son = &pool[i];
            pool[i].father = latest_entry;

            latest_entry = &pool[i];
            return &pool[i];
        }
    }

    return NULL;
}


u64 alloc_pages(u64 pages) {
    Page_Pool *entry = pool_start;

    while (entry != NULL) {
        if (entry->PageCount >= pages) {
            u64 addr = entry->PageBase;

            if (entry->PageCount == pages) {
                delete_pool(entry);
            } else if (entry->PageCount > pages) {
                entry->PageBase += pages << 12;
                entry->PageCount -= pages;
            }

            memset((void *)addr, 0, pages<<12);
            return addr;
        }

        entry = entry->Son;
    }

     
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(pages);
    memset((void *)alloc.PhysicalStart, 0, alloc.NumberOfPages<<12);
    return alloc.PhysicalStart;
}



void free_pages(u64 address, u64 pages) {
    latest_entry = find_new_entry();
    if (latest_entry == NULL) {
        EFI_MEMORY_DESCRIPTOR alloc = {0};
        alloc.PhysicalStart = address;
        alloc.NumberOfPages = pages;
        free_frame(alloc);
        return;
    }

    if (pool_start == NULL) 
        pool_start = latest_entry;
    
    latest_entry->PageBase = address;
    latest_entry->PageCount = pages;

}

