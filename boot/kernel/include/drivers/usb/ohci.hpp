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


typedef struct ohci_ed ohci_ed;
typedef struct ohci_td ohci_td;

struct alignas(16) ohci_ed {
    u32 control;
    u32 tailp;
    u32 headp;
    u32 next_ed;
};

struct alignas(16) ohci_td {
    u32 control;
    u32 cbp;       // Current Buffer Pointer
    u32 next_td;
    u32 be;        // Buffer End
};

static_assert(sizeof(ohci_ed) == 16);
static_assert(sizeof(ohci_td) == 16);

typedef struct {
    u8 device_address;
    u8 endpoint_address;
    u8 transfer_type;
    u8 interval;
    bool low_speed;

    u16 max_packet_size;

    bool scheduled;
    dma_ret dma;

    ohci_ed *ed;
    u32 ed_phys;

    ohci_td *dummy_td;
    u32 dummy_td_phys;

    ohci_td *data_td;
    u32 data_td_phys;

    u32 buffer_phys;
    void *buffer;
} ohci_endpoint_state;

typedef struct {
    u8 interface_number;
    ohci_endpoint_state *endpoints;
    u16 endpoint_count;
} ohci_interface_state;


typedef struct {
    u8 device_address;
    ohci_interface_state *interfaces;
    u16 interfaces_count;
} ohci_address_state;


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

        ohci_address_state *states;
        u8 state_count;
        ohci_endpoint_state configure_endpoint(usb_device device, u16 endpoint_index, u16 interface_index, TimerSource &timer);
        bool link_ed(ohci_endpoint_state &ep, TimerSource &timer);
        bool unlink_ed(ohci_endpoint_state &ep, TimerSource &timer);

    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        int disable_port(u8 port, TimerSource &timer);
        usb_device_descriptor identify_device(u8 port, TimerSource &timer);
        usb_device set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port);
        void *usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port);
        int configure_endpoints(usb_device device, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer);
        usb_transfer_result usb_transfer(u32 endpoint_address, void *buffer, u32 length, TimerSource &timer, u32 timeout);
        ohci_controller();

};
