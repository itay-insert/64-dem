#ifndef DMA_INTERNAL_H
#define DMA_INTERNAL_H

#include <stddef.h>
#include "x86-64/paging.h"

#define Free 0
#define Used 1
#define Header 2
#define NoEntriesLeft 0

typedef struct dma_entry dma_entry;

struct dma_entry {
    int status;
    u64 SizeInPages;
    dma_entry *next_entry;
} __attribute__((packed));

typedef struct {
    char sign[8];
    int status;
    u64 SizeInPages;
    dma_entry *home_entry;
} __attribute__((packed)) dma_descriptor;

extern int entries;
extern dma_entry *dma_header;
extern dma_entry *dma_start;
extern dma_entry *dma_latest;
extern int limit;
extern int metadata_pages;
extern u64 dma_top;

dma_entry *find_header(dma_entry *entry);
u64 find_descriptorBase(u64 Base);

#endif
