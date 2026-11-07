#pragma once
#include "drivers/pci/irq.h"

// Cached registers let the top half mask the source without virtual dispatch,
// allocations, diagnostics, schedule manipulation, or event-ring consumption.
struct usb_irq_state {
    pci_irq_route route = {};
    u8 kind = 0;
    u16 io_base = 0;
    volatile u32 *status = nullptr;
    volatile u32 *enable = nullptr;
    volatile u32 *disable = nullptr;
    volatile u32 *command = nullptr;
    u32 enable_mask = 0;
    u32 pending = 0;
    u64 count = 0;
    bool ready = false;
};
void usb_irq_top_half(void *context);
void usb_irq_mask(usb_irq_state &state);
void usb_irq_unmask(usb_irq_state &state);
