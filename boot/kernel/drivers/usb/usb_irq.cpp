#include "drivers/usb/usb.hpp"
#include "x86-64/ports.h"
#include "x86-64/lowlevel.h"
#include "drivers/display/vga.h"

// This entire translation unit is compiled with -mgeneral-regs-only: the top
// half and everything it calls preserve the interrupted SIMD/FPU state.
void usb_irq_mask(usb_irq_state &s) {
    if (s.kind == 0) outw(s.io_base + 4, 0);
    else if (s.kind == 1) { *s.disable = 1u << 31; (void)*s.enable; }
    else if (s.kind == 2) { *s.enable = 0; (void)*s.enable; }
    else { *s.enable = 0; (void)*s.enable; } // IMAN: IE=0; do not clear RW1C IP.
}
void usb_irq_unmask(usb_irq_state &s) {
    if (s.kind == 0) outw(s.io_base + 4, (u16)s.enable_mask);
    else { *s.enable = s.enable_mask; (void)*s.enable; }
}
void usb_irq_top_half(void *context) {
    auto &s = *static_cast<usb_irq_state *>(context);
    if (!s.ready) return;
    bool asserted;
    if (s.kind == 0)
        asserted = inw(s.io_base + 4) && (inw(s.io_base + 2) & 0x1fu);
    else if (s.kind == 1)
        asserted = (*s.enable & (1u << 31)) && (*s.status & *s.enable & 0x7fu);
    else if (s.kind == 2)
        asserted = (*s.status & *s.enable & 0x3fu) != 0;
    else asserted = (*s.enable & 3u) == 3u; // IMAN.IP && IMAN.IE
    if (!asserted) return;
    // Masking deasserts the source while retaining DMA/status ownership for the
    // foreground worker. In particular, OHCI WDH must not be cleared here.
    usb_irq_mask(s);
    __atomic_fetch_add(&s.count, 1ULL, __ATOMIC_RELAXED);
    __atomic_store_n(&s.pending, 1u, __ATOMIC_RELEASE);
}

bool usb_controller::usb_setup_irq() {
    if (irq.ready) return true;
    if (status || !controller.io_base) return false;
    irq.kind = prog_if >> 4;
    const u64 base = controller.io_base;
    if (irq.kind == 0) {
        irq.io_base = (u16)base;
        irq.enable_mask = 0x0f; // timeout/CRC, resume, IOC, short packet.
    } else if (irq.kind == 1) {
        irq.status = (volatile u32 *)(base + 0x0c);
        irq.enable = (volatile u32 *)(base + 0x10);
        irq.disable = (volatile u32 *)(base + 0x14);
        irq.enable_mask = (1u << 31) | (1u << 1) | (1u << 4); // MIE, WDH, UE.
    } else if (irq.kind == 2) {
        const u8 cap_length = *(volatile u8 *)base;
        irq.status = (volatile u32 *)(base + cap_length + 4);
        irq.enable = (volatile u32 *)(base + cap_length + 8);
        irq.enable_mask = 0x13; // USBINT, USBERRINT, host-system error; IAA stays polled.
    } else if (irq.kind == 3) {
        const u8 cap_length = *(volatile u8 *)base;
        const u32 runtime = *(volatile u32 *)(base + 0x18) & ~0x1fu;
        irq.status = (volatile u32 *)(base + cap_length + 4);
        irq.command = (volatile u32 *)(base + cap_length);
        irq.enable = (volatile u32 *)(base + runtime + 0x20);
        irq.enable_mask = 2; // Interrupter zero IE.
    } else return false;
    usb_irq_mask(irq);
    // Do not infer a PCI INTx GSI from the config-space Interrupt Line byte.
    // Legacy machines keep using the same foreground transfer API via polling.
    const u8 destination = lapic_read(0x20) >> 24;
    if (!pci_irq_setup(controller.pci_address, destination, usb_irq_top_half, &irq, &irq.route)) {
        printf("USB.%s: no MSI/MSI-X route; using polling\n", vendor);
        return false;
    }
    irq.ready = true;
    if (irq.kind == 3) {
        *irq.command |= 1u << 2; // USBCMD.INTE, after route and event ring exist.
        (void)*irq.command;
    }
    // Clear/drain boot-time status in foreground before unmasking.
    usb_begin_irq_service();
    usb_end_irq_service();
    printf("USB.%s: %s IRQ vector 0x%b installed\n", vendor,
        irq.route.mode == PCI_IRQ_MSI ? "MSI" : "MSI-X", irq.route.vector);
    return true;
}
void usb_controller::usb_begin_irq_service() {
    if (!irq.ready) return;
    usb_irq_mask(irq);
    __atomic_exchange_n(&irq.pending, 0u, __ATOMIC_ACQ_REL);
    if (irq.kind == 0) {
        const u16 status = inw(irq.io_base + 2) & 0x1fu;
        if (status & 0x18u) controller.needs_reinit = true;
        outw(irq.io_base + 2, status);
    } else if (irq.kind == 2) {
        const u32 status = *irq.status & 0x13u;
        if (status & 0x10u) controller.needs_reinit = true;
        *irq.status = status; // Never clear polled IAA or schedule status.
    }
    service_irq_events(); // OHCI DoneHead and xHCI event-ring work stays foreground.
}
void usb_controller::usb_end_irq_service() {
    if (irq.ready && !controller.needs_reinit) usb_irq_unmask(irq);
}
