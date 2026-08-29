#include <stddef.h>
#include <stdbool.h>
#include "x86-64/paging.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/frame_allocator.h"
#include "x86-64/memory/virtual_allocator.h"
#include "x86-64/memory/malloc_internal.h"


heap_entry *create_entry(u64 size, u64 base, u16 attributes, int Status) {
    if (heap_header->SizeInBytes != NoEntriesLeft && heap_header->next_entry == NULL) {
        
    }
}