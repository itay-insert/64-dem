#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

typedef struct uhci_td uhci_td;

struct alignas(16) uhci_td {
    u32 link;
    u32 status;
    u32 token;
    u32 buffer;


    u32 software[4];
};

static_assert(alignof(uhci_td) == 16);

typedef struct uhci_qh uhci_qh;

struct alignas(16) uhci_qh {
    u32 head_link;
    u32 element_link;
    u32 reserved[2];
};

static_assert(sizeof(uhci_qh) == 16);


typedef struct {
    u8 device_address;
    u8 endpoint_address;
    u8 interval;
    u8 data_toggle;
    u16 max_packet_size;
    
    dma_ret dma;
    uhci_qh *qh;
    uhci_td *td;
    u32 qh_phys;
    u32 td_phys;
    u32 buffer_phys;
} uhci_endpoint_state;

typedef struct {
    u8 interface_number;
    uhci_endpoint_state *endpoints;
    u16 endpoint_count;
} uhci_interface_state;


typedef struct {
    u8 device_address;
    uhci_interface_state *interfaces;
    u16 interfaces_count;
} uhci_address_state;

class uhci_controller : public usb_controller {
    private:
        u32 *frame_list;
        uhci_address_state *states;
        u8 state_count;

    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        int disable_port(u8 port, TimerSource &timer);
        usb_device_descriptor identify_device(u8 port, TimerSource &timer);
        usb_device set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port);
        void *usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port);
        int configure_endpoints(usb_device device, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer);
        uhci_controller();

};
