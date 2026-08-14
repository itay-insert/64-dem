#include "x86-64/memory/dma_internal.h"

int entries = 0;

dma_entry *dma_header = NULL;
dma_entry *dma_start = NULL;

dma_entry *dma_latest = NULL;

int limit = 0;
int metadata_pages = 0;

u64 dma_top = DMA_POOL;
