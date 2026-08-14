#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/dma_internal.h"


dma_entry *find_header(dma_entry *entry) {
    while (entry->status != Header) {
        entry = (dma_entry *)((u8 *)entry - sizeof(dma_entry));
    } 
        
    return entry;
}



u64 find_descriptorBase(u64 Base) {
    while (1) {
        dma_descriptor *ptr = (dma_descriptor *)Base;
        if (memcmp(ptr->sign, "DMA_POOL", 8) == 0) 
            return Base;
        Base++;
    }
}
