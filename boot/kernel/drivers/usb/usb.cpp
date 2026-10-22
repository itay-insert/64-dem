#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/usb/usb.hpp"

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


