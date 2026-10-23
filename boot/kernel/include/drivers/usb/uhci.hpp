#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

class uhci_controller : public usb_controller {
    public:
        int controller_init(TimerSource &timer) override;
        uhci_controller();

};