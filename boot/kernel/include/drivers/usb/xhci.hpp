#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/xhci_events.h"
#include "drivers/usb/usb.hpp"

class xhci_controller : public usb_controller {
    public:
        int controller_init(TimerSource &timer) override;
        xhci_controller();

};