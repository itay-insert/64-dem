#ifndef XHCI_EVENTS_H
#define XHCI_EVENTS_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

/* Drain and classify pending xHCI events; returns the number consumed.
 * Current IRQ delivery masks the source and defers this work to foreground. */
u32 xhci_process_events(void);

/* Returns 1 once for each port with a pending status-change notification.
 * The port worker must inspect PORTSC and clear its change bits. */
int xhci_take_port_change(u8 port_number);

/* Legacy software flag; routing/enabling is owned by usb_setup_irq().
 * Command waits retain polling fallback even when hardware IRQs are routed. */
void xhci_set_interrupt_driven(int enabled);

KERNEL_EXTERN_C_END

#endif
