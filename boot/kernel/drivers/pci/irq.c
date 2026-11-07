#include "drivers/pci/irq.h"

static int read_config(pci_address_t a, u16 offset, u8 size, u32 *value) {
    return pci_config_read(a, offset, size, value) == PCI_STATUS_SUCCESS;
}
static int write_config(pci_address_t a, u16 offset, u8 size, u32 value) {
    return pci_config_write(a, offset, size, value) == PCI_STATUS_SUCCESS;
}
static int disable_capability(pci_address_t a, u16 cap, u16 enable) {
    u32 control;
    return !cap || (read_config(a, cap + 2, 2, &control) &&
        write_config(a, cap + 2, 2, control & ~enable));
}
int pci_irq_disable(pci_irq_route *route) {
    if (!route || !route->vector) return 1;
    if (!disable_capability(route->address, route->capability,
            route->mode == PCI_IRQ_MSIX ? 0x8000 : 1)) return 0;
    if (route->table) {
        route->table[3] = 1;
        (void)route->table[3];
    }
    device_irq_release(route->vector);
    route->vector = 0;
    route->mode = PCI_IRQ_POLLING;
    return 1;
}
int pci_irq_setup(pci_address_t a, u8 destination, device_irq_handler handler,
                  void *context, pci_irq_route *route) {
    if (!route || route->vector || !handler) return 0;
    *route = (pci_irq_route){.address = a};
    u16 msi = 0, msix = 0;
    int status = pci_find_capability(a, 0x05, &msi);
    if (status != PCI_STATUS_SUCCESS && status != PCI_STATUS_NOT_FOUND) return 0;
    status = pci_find_capability(a, 0x11, &msix);
    if (status != PCI_STATUS_SUCCESS && status != PCI_STATUS_NOT_FOUND) return 0;
    // Disable firmware interrupt modes before publishing our route.
    if (!disable_capability(a, msi, 1) || !disable_capability(a, msix, 0x8000) ||
        pci_set_command_bits(a, PCI_COMMAND_INTX_DISABLE, 0) != PCI_STATUS_SUCCESS)
        return 0;
    if (!msi && !msix) return 0;
    route->vector = device_irq_allocate(handler, context);
    if (!route->vector) return 0;
    const u32 message_address = 0xfee00000u | ((u32)destination << 12);
    u32 control;
    if (msi) {
        route->capability = msi;
        route->mode = PCI_IRQ_MSI;
        if (!read_config(a, msi + 2, 2, &control)) goto failure;
        const int wide = (control & 0x80) != 0;
        const u16 data_offset = wide ? 12 : 8;
        const u16 mask_offset = wide ? 16 : 12;
        if (msi + data_offset + 2 > 256 ||
            ((control & 0x100) && msi + mask_offset + 4 > 256)) goto failure;
        control &= ~0x71u; // Disable and select a single message.
        if (!write_config(a, msi + 2, 2, control) ||
            !write_config(a, msi + 4, 4, message_address) ||
            (wide && !write_config(a, msi + 8, 4, 0)) ||
            !write_config(a, msi + data_offset, 2, route->vector)) goto failure;
        if (control & 0x100) {
            // Only message zero is used. Keep all other messages masked.
            if (!write_config(a, msi + mask_offset, 4, 0xfffffffeu)) goto failure;
        }
        if (!write_config(a, msi + 2, 2, control | 1u)) goto failure;
        return 1;
    }
    route->capability = msix;
    route->mode = PCI_IRQ_MSIX;
    u32 descriptor;
    if (msix + 12 > 256 || !read_config(a, msix + 2, 2, &control) ||
        !read_config(a, msix + 4, 4, &descriptor)) goto failure;
    const u8 bir = descriptor & 7;
    const u32 offset = descriptor & ~7u;
    const u32 entries = (control & 0x7ff) + 1;
    if (bir >= 6) goto failure;
    // Reject a BIR naming the upper half of a 64-bit BAR.
    for (u8 bar_index = 0; bar_index < bir; ++bar_index) {
        u32 bar;
        if (!read_config(a, 0x10 + 4 * bar_index, 4, &bar)) goto failure;
        if (!(bar & 1) && (bar & 6) == 4) {
            if (++bar_index == bir) goto failure;
        }
    }
    pci_bar_t bar;
    if (!write_config(a, msix + 2, 2, (control & ~0x8000u) | 0x4000u) ||
        pci_read_bar(a, bir, 1, &bar) != PCI_STATUS_SUCCESS ||
        bar.type != PCI_BAR_MEMORY || !bar.address || !bar.size ||
        offset > bar.size || (u64)entries * 16 > bar.size - offset ||
        bar.address > ~0ULL - offset) goto failure;
    route->table = pci_irq_map_mmio(bar.address + offset, (u64)entries * 16);
    if (!route->table) goto failure;
    // Mask stale firmware table entries before enabling the function.
    for (u32 i = 0; i < entries; ++i) route->table[i * 4 + 3] = 1;
    route->table[0] = message_address;
    route->table[1] = 0;
    route->table[2] = route->vector;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    route->table[3] = 0;
    (void)route->table[3];
    if (!write_config(a, msix + 2, 2, (control | 0x8000u) & ~0x4000u)) goto failure;
    return 1;
failure:
    // If a config failure prevents disabling, keep the vector/context reserved.
    // Controller sources remain masked, and the controller outlives this route.
    pci_irq_disable(route);
    return 0;
}
