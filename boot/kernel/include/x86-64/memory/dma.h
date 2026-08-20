#ifndef DMA_H
#define DMA_H

#include "uint_definitions.h"

typedef struct {
    int status;
    u64 SizeInPages;
    u64 virtual_address;
    u64 physical_address;
} dma_ret;

dma_ret allocate_dma(u64 size);
void free_dma(u64 base);

#endif
