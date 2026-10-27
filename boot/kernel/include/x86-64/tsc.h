#ifndef TSC_H
#define TSC_H

#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN

#include "uint_definitions.h"
#include "x86-64/lowlevel.h"

static inline void wait_tsc(u64 ms) {
    u64 msticks = ms * 1000;
    u64 duration = tsc_freq() / msticks;
    u64 start = read_tsc();
    while (read_tsc() - start < duration)
        __asm__ volatile("pause");
}



KERNEL_EXTERN_C_END
#endif