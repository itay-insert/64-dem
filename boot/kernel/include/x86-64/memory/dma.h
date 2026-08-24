#ifndef DMA_H
#define DMA_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

typedef struct {
    int status;
    u64 SizeInPages;
    u64 virtual_address;
    u64 physical_address;
} dma_ret;

dma_ret allocate_dma(u64 size);
void free_dma(u64 base);

KERNEL_EXTERN_C_END

#endif
