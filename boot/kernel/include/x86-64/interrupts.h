#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN
/* Signed offsets from exception_stub_table, not link-time pointers. */
extern const intptr_t exception_stub_table[32];
extern const intptr_t spurious_stub;
extern const intptr_t timer_stub;
void isr_eoi(void);
KERNEL_EXTERN_C_END

#endif
