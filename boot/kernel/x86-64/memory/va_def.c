#include "x86-64/memory/va_alloc_internal.h"
#include "x86-64/spinlock.h"


int va_entries = 0;
int max = 0;

va_hd *va_header = NULL;
va_node *va_kstart = NULL;
va_node *va_ustart = NULL;
va_node *va_latest = NULL;

int va_limit = 0;
int va_metadata_pages = 0;

u64 va_top = VA_TREE;

spinlock_t va_lock = {0};
