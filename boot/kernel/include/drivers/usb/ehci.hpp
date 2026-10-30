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

class ehci_controller : public usb_controller {
    private:
        u32 *Frame_list;
        ehci_qh *async_head;
        u32 async_head_phys;
        u64 port_to_address(u8 port);
        int disable_port(u8 port, TimerSource &timer);

    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        usb_device_descriptor identify_device(u8 port, TimerSource &timer);
        ehci_controller();

};