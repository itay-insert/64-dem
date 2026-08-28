#include "x86-64/io.h"
#include "drivers/timers/pm.hpp"

#define PM_TIMER_FREQUENCY 3579545

u64 PMTimer::read() {
    return io_inl(hw.Timer_base, hw.Mode);
}

u64 PMTimer::read_freq() {
    return PM_TIMER_FREQUENCY;
}