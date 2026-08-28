#pragma once
#include "x86-64/acpi.h"


class SimpleTimer {
    public:
        u64 Timer_base;
        int Mode;
        char signature[5];
        void Timer_init(PM_ret desc, const char *name, const char *sign);
};

class TimerSource {
    public:
        virtual u64 read() = 0;
        virtual u64 read_freq() = 0;
}

class Timer {
    public: 
        SimpleTimer *Src;
        Timer();
};