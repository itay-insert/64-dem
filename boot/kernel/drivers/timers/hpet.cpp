#include "x86-64/io.h"
#include "drivers/timers/hpet.hpp"


void HPET::init() {
    const u64 base = hw.Timer_base;       // enable the hpet counter
    const u64 cap = io_inq(base, hw.Mode);
    clong = (cap & (1ULL << 13)) != 0;

    const u64 config = io_inq(base + 0x10, hw.Mode);
    io_outq(base + 0x10, config | 1ULL, hw.Mode);
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