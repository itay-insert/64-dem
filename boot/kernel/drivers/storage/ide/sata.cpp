#include <stdbool.h>
#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"


typedef struct {
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u64 io_phys;
    u64 io_virt;
    pci_address_t address;
    bool discovered;
} ahci_descriptor;

ahci_descriptor ahci = {0};

void ahci_find_callback(const PCI_ret *device, void *context) {
    ahci_descriptor *desc = (ahci_descriptor *)context;

    if (desc->discovered || device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != desc->class_code || 
        device->common.subclass != desc->subclass ||
        device->common.prog_if != desc->prog_if)
        return;

    desc->address = (pci_address_t) {
        .segment = device->Segment,
        .bus = device->Bus,
        .device = device->Device,
        .function = device->Function
    };

    desc->discovered = true;
}


bool discover_ahci(void) {
    ahci_descriptor *desc = &ahci;
    *desc = {};

    desc->class_code = 0x01;
    desc->subclass = 0x06;
    desc->prog_if = 0x01;

    pci_enumerate(ahci_find_callback, desc);

    if (!desc->discovered)
        return false;

    pci_bar_t bar = {0};
    if (pci_read_bar(desc->address, 5, 0, &bar) != PCI_STATUS_SUCCESS)
        return false;

    u64 addr = bar.address;
    desc->io_phys = addr;
    desc->io_virt = desc->io_phys + BASE;

    u64 page_phys = addr & ~4095ULL;
    u64 offset = addr - page_phys;

    u64 pages = (offset + bar.size + 4095) >> 12;

    create_mapping(page_phys+BASE, page_phys, pages, 0x13, KernelPML4);
    flush_pages(page_phys+BASE, pages);



}