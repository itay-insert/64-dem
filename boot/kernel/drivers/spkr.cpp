#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "drivers/pcspkr.h"
#include "drivers/timers/timer.hpp"

extern u64 timeradd;

extern "C" void beep(unsigned int hz, u64 ms) {
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;
    speaker_on(hz);
    wait_ms(timer_source, ms);
    speaker_off();
}