#include "x86-64/io.h"
#include "drivers/timers/hpet.hpp"


HPET::HPET() {
    u64 cap = io_inq(hw.Timer_base, hw.Mode);

    clong = (cap >> 13) & 1;  // clong determines whether the tick register is 64-bit or 32-bit, extracting clong from the cap register
}


u64 HPET::read() {
    if (clong)
        return io_inq(hw.Timer_base+0xF0, hw.Mode);
    else 
        return io_inl(hw.Timer_base+0xF0, hw.Mode);
}


u64 HPET::read_freq() {
    u64 cap = io_inq(hw.Timer_base, hw.Mode);

    u32 period_fs = (u32)(cap >> 32); // extracting period_fs from the cap register

    u64 freq = 1000000000000000ULL / period_fs; // calculating the frequency from period_fs

    return freq;
}