#pragma once

#include "drivers/timers/timer.hpp"


class HPET : public TimerSource {

    private:
        bool clong;
    

    public:
        void init();
        u64 read() override;
        u64 read_freq() override;
        
};