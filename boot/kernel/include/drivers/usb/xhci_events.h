#ifndef XHCI_EVENTS_H
#define XHCI_EVENTS_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

/* Drain and classify pending xHCI events; returns the number consumed.
 * A future ISR can call this, then acknowledge its CPU interrupt separately. */
u32 xhci_process_events(void);

/* Returns 1 once for each port with a pending status-change notification.
 * The port worker must inspect PORTSC and clear its change bits. */
int xhci_take_port_change(u8 port_number);

/* Call after interrupt routing is enabled, so command waits stop polling. */
void xhci_set_interrupt_driven(int enabled);

KERNEL_EXTERN_C_END

#endif
