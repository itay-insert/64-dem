#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN
/* Signed offsets from exception_stub_table, not link-time pointers. */
extern const intptr_t exception_stub_table[32];
extern const intptr_t spurious_stub;
extern const intptr_t timer_stub;
extern const intptr_t keyboard_stub;
extern u64 save_rax;
extern u64 save_rbx;
extern u64 save_rcx;
extern u64 save_rdx;
extern u64 save_rsi;
extern u64 save_rdi;
extern u64 save_rbp;
extern u64 save_r8;
extern u64 save_r9;
extern u64 save_r10;
extern u64 save_r11;
extern u64 save_r12;
extern u64 save_r13;
extern u64 save_r14;
extern u64 save_r15;
void isr_eoi(void);
KERNEL_EXTERN_C_END

#endif
