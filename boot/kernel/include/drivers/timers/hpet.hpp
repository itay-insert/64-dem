#pragma once

#include "drivers/timers/timer.hpp"


class HPET : public TimerSource {

    private:
        SimpleTimer hw;
        bool clong;
    

    public:
        HPET();
        u64 read() override;
        u64 read_freq() override;
        
}