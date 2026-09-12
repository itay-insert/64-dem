#ifndef VA_ALLOC_H
#define VA_ALLOC_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

#include "x86-64/memory/va_alloc_internal.h"

va_ret va_alloc(u64 pages, u16 attributes);
void va_free(va_ret desc);


KERNEL_EXTERN_C_END

#endif