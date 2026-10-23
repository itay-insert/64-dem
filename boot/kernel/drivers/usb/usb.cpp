#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/usb/usb.hpp"
#include "drivers/timers/timer.hpp"


extern u64 timeradd;


void usb_find_callback(const PCI_ret *device, void *context) {
    usb_controller_t *controller = static_cast<usb_controller_t *>(context);

    if (controller->found || device->PCI_status != PCI_STATUS_SUCCESS) 
        return;

    if (device->common.class_code != controller->class_code ||
        device->common.subclass != controller->subclass ||
        device->common.prog_if != controller->prog_if)
        return;


    controller->pci_address = (pci_address_t) {
        .segment  = device->Segment,
        .bus      = device->Bus,
        .device   = device->Device,
        .function = device->Function,
    };

    controller->found = true;

}


bool usb_controller::discover_controller(usb_controller_t *controller, u8 class_code, u8 subclass, u8 prog_if, const char *str) {
    *controller = {};

    controller->class_code = class_code;
    controller->subclass = subclass;
    controller->prog_if = prog_if;

    pci_enumerate(usb_find_callback, controller);

    if (!controller->found) {
        printf("USB: no controller found!\n");
        return false;
    }

    printf("%s controller found at %u:%u:%u.%u\n", str,
        (unsigned int)controller->pci_address.segment,
        (unsigned int)controller->pci_address.bus,
        (unsigned int)controller->pci_address.device,
        (unsigned int)controller->pci_address.function);

    return true;
}


void usb_controller::MapController(usb_controller_t *controller, u8 type) {
    if (!controller || type > 1) // exit if the arguements are invalid
        return;

    PCI_ret device = pci_get_info(controller->pci_address);

    if (device.PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (type == IO) {
        pci_bar_t bar = {0};
        if (pci_read_bar(controller->pci_address, 4, 0, &bar) != PCI_STATUS_SUCCESS)
            return;
        controller->io_base = (u16)bar.address;
        controller->bar = bar;
        return;
    }
    u8 header_type = device.common.header_type & 0x7F;
    u8 bar_count = 0;

    switch (header_type) {
        case 0x00:
            bar_count = 6;
            break;
        case 0x01:
            bar_count = 2;
            break;
        default:
            return;
    }

    for (u8 i = 0; i < bar_count; i++) {
        pci_bar_t bar = {0};

        if (pci_read_bar(controller->pci_address, i, 1, &bar) != 
        PCI_STATUS_SUCCESS) 
            continue;

        if (bar.type == MMIO) {
            u64 addr = bar.address; // pci_read_bar handles bar.address conversion for us
            u64 phys_start = addr & ~4095ULL; // align to 4K
            u64 offset = bar.address - phys_start; // calculate the offset
            u64 pages = (offset + bar.size + 4095) >> 12; // >> 12 == / 4096, calculate the number of pages to map
            create_mapping(phys_start+BASE, phys_start, pages, 0x13, KernelPML4); // map the MMIO address into the virtual i/o space
            flush_pages(phys_start+BASE, pages); // flush the mapped pages
            addr = addr + BASE; // pass the virtual address to controller->io_base instead of the physical one
            
            controller->io_base = addr; // else, pass the physical IO address to controller->io_base
            controller->bar = bar; // save the pci bar to controller->bar          
            return;
        }
    }

    printf("USB: [BAR_ERROR] no matching bar for the %s controller\n", vendor);

}

void usb_controller::construct_controller() {
    // controller discovery
    bool dists = discover_controller(&controller, class_code, subclass, prog_if, vendor); // find the controller's pci_address
    if (!dists)
        return; // return if there was a discovery failure
    MapController(&controller, type); // map the controller to the virtual MMIO space if needed


    // controller pci requirements
    if (pci_enable_device(controller.pci_address) != PCI_STATUS_SUCCESS) { // enable the pci device
        printf("%s: failed to enable pci device", vendor); // print a message if error
        return; // abort if failure
    }

    if (pci_enable_bus_mastering(controller.pci_address) != PCI_STATUS_SUCCESS) { // enable pci bus mastering
        printf("%s: failed to enable bus mastering", vendor); // print a message if error
        return; // abort if failure
    }

    
    Timer *timer = reinterpret_cast<Timer *>(timeradd); // get a pointer to the global timer class
    status = controller_init(*timer->Src);
}


