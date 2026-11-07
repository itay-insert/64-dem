#include <assert.h>
#include <stdio.h>
#include "drivers/usb/irq.hpp"

int main() {
    u32 status = 1;
    u32 enable = 1;
    u32 disable = 0;
    usb_irq_state irq;
    irq.kind = 2; // EHCI
    irq.status = &status;
    irq.enable = &enable;
    irq.ready = true;
    usb_irq_top_half(&irq);
    assert(enable == 0 && status == 1 && irq.pending == 1 && irq.count == 1);
    usb_irq_top_half(&irq); // Already masked; no duplicate work.
    assert(irq.count == 1);
    irq.pending = 0;
    irq.kind = 3; // xHCI IMAN contains IE and RW1C IP.
    enable = 3;
    usb_irq_top_half(&irq);
    assert(enable == 0 && irq.pending == 1 && irq.count == 2);
    // Writing zero masks IE without writing one to IP (real hardware retains IP).
    irq.kind = 1; // OHCI: mask master, retain WDH ownership for foreground.
    irq.disable = &disable;
    status = 2;
    enable = (1u << 31) | 2;
    usb_irq_top_half(&irq);
    assert(disable == (1u << 31) && status == 2 && irq.count == 3);
    irq.pending = 0;
    enable = 2; // Master disabled: ignore.
    usb_irq_top_half(&irq);
    assert(irq.pending == 0 && irq.count == 3);
    irq.ready = false;
    enable = (1u << 31) | 2;
    usb_irq_top_half(&irq);
    assert(irq.count == 3);
    puts("USB IRQ top-half tests passed");
}
