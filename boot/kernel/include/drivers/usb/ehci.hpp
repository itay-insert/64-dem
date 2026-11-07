#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

typedef struct ehci_qh ehci_qh;
typedef struct ehci_qtd ehci_qtd;

struct alignas(32) ehci_qh {
    uint32_t horizontal_link;
    uint32_t endpoint_characteristics;
    uint32_t endpoint_capabilities;
    uint32_t current_qtd;

    uint32_t next_qtd;
    uint32_t alternate_next_qtd;
    uint32_t token;
    uint32_t buffer[5];

    uint32_t extended_buffer[5];
};

struct alignas(32) ehci_qtd {
    uint32_t next_qtd;
    uint32_t alternate_next_qtd;
    uint32_t token;
    uint32_t buffer[5];

    uint32_t extended_buffer[5];
};

static_assert(sizeof(ehci_qh) == 96);
static_assert(sizeof(ehci_qtd) == 64);

static_assert(alignof(ehci_qh) == 32);
static_assert(alignof(ehci_qtd) == 32);

typedef struct {
    u8 device_address;
    u8 endpoint_address;
    u8 transfer_type;
    u8 interval;

    u8 speed;

    u16 max_packet_size;
    u8 mult;
    bool scheduled;

    dma_ret dma;
    ehci_qh *qh;
    u32 qh_phys;
    ehci_qtd *dummy_qtd;
    u32 dummy_qtd_phys;

    ehci_qtd *data_qtd;
    u32 data_qtd_phys;

    u32 buffer_phys;
    void *buffer;
} ehci_endpoint_state;

typedef struct {
    u8 interface_number;
    ehci_endpoint_state *endpoints;
    u16 endpoint_count;
} ehci_interface_state;


typedef struct {
    u8 device_address;
    ehci_interface_state *interfaces;
    u16 interfaces_count;
} ehci_address_state;


class ehci_controller : public usb_controller {
    private:
        u32 *Frame_list;
        ehci_qh *async_head;
        u32 async_head_phys;
        u64 port_to_address(u8 port);

        ehci_address_state *states;
        u8 state_count;
        ehci_endpoint_state configure_endpoint(usb_device device, u16 endpoint_index, u16 interface_index, TimerSource &timer);
        bool link_qh(ehci_endpoint_state &ep, TimerSource &timer);
        bool unlink_qh(ehci_endpoint_state &ep, TimerSource &timer);

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
        ehci_controller();

};
