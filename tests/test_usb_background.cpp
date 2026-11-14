#include <assert.h>
#include <stdio.h>
#include "drivers/usb/usb.hpp"
#include "x86-64/lowlevel.h"
#include "x86-64/apic.h"

extern "C" {
char interrupt_idle_resume;
void usb_background_tick(u64 cs, u64 rip);
}
static unsigned services;
static u32 registers[256];
static u32 calibration = 100000;
extern "C" u32 lapic_read(u32 offset) {
    return offset == 0x390 ? 0xffffffffu - calibration : registers[offset / 4];
}
extern "C" void lapic_write(u32 offset, u32 value) { registers[offset / 4] = value; }
void wait_ms(TimerSource &, u64 ms) { assert(ms == 10); }
GlobalUSB::GlobalUSB() {}
void GlobalUSB::service_interrupts(TimerSource &) { ++services; }
class Clock : public TimerSource {
public:
    u64 frequency = 1000000;
    u64 read() override { return 0; }
    u64 read_freq() override { return frequency; }
};

int main() {
    GlobalUSB usb, other;
    Clock clock;
    usb_background_tick(0x23, 0x40000000); // Unregistered: nothing to service.
    assert(services == 0);
    clock.frequency = 0;
    assert(!usb.start_background_service(clock));
    clock.frequency = 1000000;
    calibration = 0;
    assert(!usb.start_background_service(clock));
    assert(registers[0x380 / 4] == 0);
    calibration = 100000;
    assert(lapic_timer_start(clock)); // Timer starts without any USB subscription.
    assert(services == 0);
    assert(usb.start_background_service(clock));
    assert(services == 1);
    assert(registers[0x320 / 4] == ((1u << 17) | 0x40));
    assert(registers[0x380 / 4] == 10000);
    assert(usb.start_background_service(clock)); // Idempotent, no rearming.
    assert(!other.start_background_service(clock));
    usb_background_tick(0x08, 0xffff800000000000); // Kernel operation: skip.
    assert(services == 1);
    usb_background_tick(0x23, 0x40000000); // User code: collect and rearm.
    assert(services == 2);
    usb_background_tick(0x08, (u64)&interrupt_idle_resume); // Safe idle point.
    assert(services == 3);
    puts("USB background service tests passed");
}
