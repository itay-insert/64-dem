#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/xhci_events.h"
#include "drivers/usb/usb.hpp"

class xhci_controller : public usb_controller {
    public:
        char vendors[5] = "xHCI";
        usb_controller_t controller = {0};
        u8 class_code = 0x0C;
        u8 subclass = 0x03;
        u8 prog_if = 0x30;
        u8 type = MMIO;
        int status;
        bool discover_controller(usb_controller_t *controller, u8 class_code, u8 subclass, u8 prog_if, const char *str);
        void MapController(usb_controller_t *controller, u8 type);
        int controller_init();
        usb_controller();

};