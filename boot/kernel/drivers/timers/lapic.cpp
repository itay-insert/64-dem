#include "drivers/timers/timer.hpp"
#include "x86-64/apic.h"
#include "x86-64/lowlevel.h"

// Boot CPU only. Start once, independently of USB device discovery.
bool lapic_timer_start(TimerSource &timer) {
    static bool started = false;
    if (started) return true;
    if (!timer.read_freq()) return false;

    lapic_write(0x320, (1u << 16) | 0x40);
    lapic_write(0x3e0, 3); // divide by 16
    lapic_write(0x380, 0xffffffffu);
    wait_ms(timer, 10);
    const u32 elapsed = 0xffffffffu - lapic_read(0x390);
    lapic_write(0x380, 0);
    if (elapsed < 10) return false;

    lapic_write(0x320, (1u << 17) | 0x40); // periodic, unmasked
    lapic_write(0x380, elapsed / 10); // approximately 1 kHz
    started = true;
    return true;
}
