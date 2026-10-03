#ifndef KMALLOC_H
#define KMALLOC_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN


void *kmalloc(u64 Size);
void kfree(void *alloc);




KERNEL_EXTERN_C_END

#endif