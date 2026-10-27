#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

class ohci_controller : public usb_controller {
    private:
        u64 ohci_base;
        u64 HCCA_base;

        inline u32 ohci_read_register(u64 offset) {
            volatile u32 *ohci_ptr = (volatile u32 *)(ohci_base + offset);
            return (u32)(*ohci_ptr);
        }

        inline u32 ohci_read_port(u8 port) {
            u64 offset = 0x54 + (port << 2); // offset = 0x54 + (port * 4)
            return ohci_read_register(offset);
        }



    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        ohci_controller();

};