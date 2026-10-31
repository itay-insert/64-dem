#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"


typedef struct ohci_hcca ohci_hcca;

struct alignas(256) ohci_hcca { // ohci hcca
    u32 interrupt_table[32]; 

    u16 frame_number;        // frame_number counter
    u16 padding;            // 2 bytes of padding

    u32 done_head;           

    u8 reserved[120];        
};

static_assert(sizeof(ohci_hcca) == 256);
static_assert(alignof(ohci_hcca) == 256);

class ohci_controller : public usb_controller {
    private:
        u64 ohci_base;
        u64 HCCA_base;
        ohci_hcca *hcca;

        inline u32 ohci_read_register(u64 offset) {
            volatile u32 *ohci_ptr = (volatile u32 *)(ohci_base + offset);
            return (u32)(*ohci_ptr);
        }

        inline u32 ohci_read_port(u8 port) {
            u64 offset = 0x54 + (port << 2); // offset = 0x54 + (port * 4)
            return ohci_read_register(offset);
        }

        int disable_port(u8 port, TimerSource &timer);



    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        usb_device_descriptor identify_device(u8 port, TimerSource &timer);
        ohci_controller();

};