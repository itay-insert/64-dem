#ifndef DMA_H
#define DMA_H

#include "uint_definitions.h"

u64 allocate_dma(u64 size);
void free_dma(u64 base);

#endif
