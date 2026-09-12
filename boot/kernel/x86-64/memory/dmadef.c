#include "x86-64/memory/dma_internal.h"
#include "x86-64/spinlock.h"

int dma_entries = 0;

dma_entry *dma_header = NULL;
dma_entry *dma_start = NULL;

dma_entry *dma_latest = NULL;

int dma_limit = 0;
int dma_metadata_pages = 0;

u64 dma_top = DMA_POOL;

spinlock_t dma_lock = {0};
