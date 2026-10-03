#pragma once

#include "drivers/timers/timer.hpp"


class PMTimer : public TimerSource {
    public:
        u64 read() override;
        u64 read_freq() override;
};