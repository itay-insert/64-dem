#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

class ehci_controller : public usb_controller {
    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        ehci_controller();

};