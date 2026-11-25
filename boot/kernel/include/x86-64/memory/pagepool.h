#ifndef PAGEPOOL_H
#define PAGEPOOL_H

#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN

#include "uint_definitions.h"
#include <stdbool.h>

typedef struct Page_Pool Page_Pool;

struct Page_Pool {
    u64 PageBase;
    u64 PageCount;
    Page_Pool *father;
    Page_Pool *Son;
};

u64 alloc_pages(u64 pages);
void free_pages(u64 address, u64 pages);
void pool_init(void);
void pool_relocate(void);


KERNEL_EXTERN_C_END

#endif