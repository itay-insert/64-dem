#include <stddef.h>
#include <stdbool.h>
#include "x86-64/memory/memory.h"
#include "x86-64/memory/pagepool.h"


Page_Pool *pool = NULL; // pool pointer
Page_Pool *start = NULL;

Page_Pool *latest_entry = NULL;
int pools_count = 0;
u64 size = 0;
u64 entries = 0;
u64 max_entry_index = 0;

bool relocated = false;

void pool_init(void) {
    EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(2048); // allocate 8 megabytes for pool list

    pool = (Page_Pool *)alloc.PhysicalStart;
    memset(pool, 0, (2048<<12)); // zero-initialize
    size = 2048 << 12;

    start = pool;
    max_entry_index++;

    latest_entry = start;

    entries = size / sizeof(Page_Pool);

    alloc = alloc_frame(8192); // allocate 32 megabytes of memory for internal pool memory

    pool->PageBase = alloc.PhysicalStart;
    pool->PageCount = 8192;

    pool->father = NULL;
    pool->Son = NULL;

    pools_count++;
}

static void delete_pool(Page_Pool *entry) {
    if (entry == NULL)
        return; // invalid entry


    if (start == entry)
        start = NULL;

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
    Page_Pool *entry = start;

    while (entry != NULL) {
        if (entry->PageCount >= pages) {
            u64 addr = entry->PageBase;

            if (entry->PageCount == pages) {
                delete_pool(entry);
            } else if (entry->PageCount > pages) {
                entry->PageBase += pages << 12;
                entry->PageCount -= pages;
            }

            return addr;
        }

        entry = entry->Son;
    }

    if (relocated) { 
        EFI_MEMORY_DESCRIPTOR alloc = alloc_frame(pages);
        return alloc.PhysicalStart;
    }
}



void free_pages(u64 address, u64 pages) {
    latest_entry = find_new_entry();
    if (latest_entry == NULL) {
        if (relocated) {
            EFI_MEMORY_DESCRIPTOR alloc = {0};
            alloc.PhysicalStart = address;
            alloc.NumberOfPages = pages;
            free_frame(alloc);
        }

        return;
    }

    if (start == NULL) 
        start = latest_entry;
    
    latest_entry->PageBase = address;
    latest_entry->PageCount = pages;

}

void relocate_pool(void) {
    
}