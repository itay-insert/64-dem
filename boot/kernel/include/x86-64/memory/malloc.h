#ifndef MALLOC_H
#define MALLOC_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

void *malloc(u64 Size, bool Map);
void free(void *alloc, bool Map);
void *realloc(void *alloc, u64 old_size, u64 new_size);


KERNEL_EXTERN_C_END