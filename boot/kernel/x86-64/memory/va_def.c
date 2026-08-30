#include "x86-64/memory/va_alloc_internal.h"
#include "x86-64/spinlock.h"


int entries = 0;

heap_entry *heap_header = NULL;
heap_entry *heap_start = NULL;

heap_entry *heap_latest = NULL;

int limit = 0;
int metadata_pages = 0;

u64 heap_top = HEAP;

spinlock_t heap_lock = {0};