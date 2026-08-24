#ifndef GDT_H
#define GDT_H

#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN
void setup_gdt(void);
KERNEL_EXTERN_C_END

#endif
