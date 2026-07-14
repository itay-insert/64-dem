#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* Signed offsets from exception_stub_table, not link-time pointers. */
extern const intptr_t exception_stub_table[32];
extern const intptr_t spurious_stub;
void isr_eoi(void);

#endif
