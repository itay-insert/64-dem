#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

class ehci_controller : public usb_controller {
    private:
        u32 *Frame_list;
    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        ehci_controller();

};