#include "drivers/usb/usb.hpp"
#include "x86-64/lowlevel.h"
#include "x86-64/apic.h"

static GlobalUSB *background_usb;
static TimerSource *background_timer;
extern "C" char interrupt_idle_resume;

bool GlobalUSB::start_background_service(TimerSource &timer) {
    if (background_usb) return background_usb == this && background_timer == &timer;
    if (!lapic_timer_start(timer)) return false;

    service_interrupts(timer); // Arm the initial request before entering user mode.
    background_timer = &timer;
    background_usb = this;
    return true;
}

extern "C" void usb_background_tick(u64 cs, u64 rip) {
    // Never preempt kernel USB, allocator, or console operations. Idle is an
    // explicit safe point; user mode cannot be inside a kernel operation.
    if (!(cs & 3) && rip != (u64)&interrupt_idle_resume) return;
    if (background_usb) background_usb->service_interrupts(*background_timer);
}
