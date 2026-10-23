#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

class uhci_controller : public usb_controller {
    public:
        char vendors[5] = "UHCI";
        usb_controller_t controller = {0};
        u8 class_code = 0x0C;
        u8 subclass = 0x03;
        u8 prog_if = 0x00;
        u8 type = IO;
        int status;
        int controller_init(TimerSource &timer) override;
        uhci_controller();

};