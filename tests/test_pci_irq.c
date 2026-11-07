#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drivers/pci/irq.h"
static u8 config[256];
static u32 table[16];
static int allocated, released, fail_write, fail_mapping;
static u16 msi, msix;
static pci_bar_t bar;
static void handler(void *context) { (void)context; }
static u32 get(u16 offset, u8 size) {
    u32 value = 0;
    memcpy(&value, config + offset, size);
    return value;
}
static void put(u16 offset, u8 size, u32 value) { memcpy(config + offset, &value, size); }
int pci_config_read(pci_address_t a, u16 offset, u8 size, u32 *value) {
    (void)a;
    assert(offset + size <= 256);
    *value = get(offset, size);
    return PCI_STATUS_SUCCESS;
}
int pci_config_write(pci_address_t a, u16 offset, u8 size, u32 value) {
    (void)a;
    if (fail_write == offset) { fail_write = -1; return PCI_STATUS_UNSUPPORTED; }
    put(offset, size, value);
    return PCI_STATUS_SUCCESS;
}
int pci_find_capability(pci_address_t a, u8 id, u16 *offset) {
    (void)a;
    *offset = id == 5 ? msi : msix;
    return *offset ? PCI_STATUS_SUCCESS : PCI_STATUS_NOT_FOUND;
}
int pci_set_command_bits(pci_address_t a, u16 set, u16 clear) {
    (void)a;
    put(4, 2, (get(4, 2) | set) & ~clear);
    return PCI_STATUS_SUCCESS;
}
int pci_read_bar(pci_address_t a, u8 index, int probe, pci_bar_t *result) {
    (void)a; (void)index;
    assert(probe == 1);
    *result = bar;
    return PCI_STATUS_SUCCESS;
}
u8 device_irq_allocate(device_irq_handler callback, void *context) {
    (void)context;
    assert(callback == handler);
    ++allocated;
    return 0x50;
}
void device_irq_release(u8 vector) { assert(vector == 0x50); ++released; }
void *pci_irq_map_mmio(u64 physical, u64 length) {
    assert(physical == bar.address && length == sizeof(table));
    return fail_mapping ? NULL : table;
}
static void reset(void) {
    memset(config, 0, sizeof(config));
    memset(table, 0, sizeof(table));
    allocated = released = fail_mapping = 0;
    fail_write = -1;
    msi = msix = 0;
    bar = (pci_bar_t){.address = 0xf0000000, .size = 4096, .type = PCI_BAR_MEMORY};
}
int main(void) {
    const pci_address_t a = {0,0,1,0};
    pci_irq_route route = {0};
    reset();
    assert(!pci_irq_setup(a, 2, handler, NULL, &route));
    assert(!allocated && (get(4,2) & PCI_COMMAND_INTX_DISABLE));
    reset(); msi = 0x50;
    put(msi+2,2,0x170); // Maskable 32-bit, multiple-message capable/enabled bits.
    assert(pci_irq_setup(a, 2, handler, NULL, &route));
    assert(route.mode == PCI_IRQ_MSI && route.vector == 0x50);
    assert(get(msi+4,4) == 0xfee02000 && get(msi+8,2) == 0x50);
    assert((get(msi+2,2) & 0x71) == 1);
    assert(get(msi+12,4) == 0xfffffffeu);
    assert(pci_irq_disable(&route) && released == 1);
    reset(); msi = 0x50; put(msi+2,2,0x180); // 64-bit + per-vector mask
    assert(pci_irq_setup(a, 3, handler, NULL, &route));
    assert(get(msi+8,4) == 0 && get(msi+12,2) == 0x50);
    assert(get(msi+16,4) == 0xfffffffeu);
    assert(pci_irq_disable(&route));
    reset(); msi = 0x50; fail_write = msi + 4;
    assert(!pci_irq_setup(a, 0, handler, NULL, &route));
    assert(allocated == 1 && released == 1 && !(get(msi+2,2) & 1));
    reset(); msix = 0x60; put(msix+2,2,3); // Four table entries, BAR0 offset0
    assert(pci_irq_setup(a, 4, handler, NULL, &route));
    assert(route.mode == PCI_IRQ_MSIX && route.table == table);
    assert(table[0] == 0xfee04000 && table[1] == 0 && table[2] == 0x50);
    assert(table[3] == 0 && table[7] == 1 && table[11] == 1 && table[15] == 1);
    assert((get(msix+2,2) & 0xc000) == 0x8000);
    assert(pci_irq_disable(&route) && table[3] == 1);
    reset(); msix = 0x60; put(msix+2,2,3); bar.size = 32;
    assert(!pci_irq_setup(a, 0, handler, NULL, &route));
    assert(released == 1);
    reset(); msix = 0x60; put(msix+2,2,3); fail_mapping = 1;
    assert(!pci_irq_setup(a, 0, handler, NULL, &route));
    assert(released == 1);
    reset(); msix = 0x60; put(msix+4,4,1); put(0x10,4,4); // BIR1 is BAR0 upper half
    assert(!pci_irq_setup(a, 0, handler, NULL, &route));
    assert(released == 1);
    puts("PCI MSI/MSI-X routing tests passed");
}
