#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/usb/usb.hpp"
#include "drivers/timers/timer.hpp"

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

usb_controller::usb_controller() {
    discover_controller(&controller, class_code, subclass, prog_if, vendor);

    status = controller_init();
}