#pragma once
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN
// Prints each active process's image/stack allocation and process bookkeeping.
// Shared kernel memory and allocations not tracked by the process are excluded.
void print_proccess_mem(void);
KERNEL_EXTERN_C_END
