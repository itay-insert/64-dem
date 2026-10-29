#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/usb/usb.hpp"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/uhci.hpp"
#include "drivers/usb/ohci.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/xhci.hpp"

extern u64 timeradd;

typedef struct {
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 count;
} USBCount;

u16 *segment = NULL;
u8 *bus = NULL;
u8 *Device = NULL;
u8 *function = NULL;

u8 Addr_count = 0;

void usb_count_controllers(const PCI_ret *device, void *context) {
    USBCount *Match = static_cast<USBCount *>(context);

    if (device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != Match->class_code || 
        device->common.subclass !=  Match->subclass ||
        device->common.prog_if != Match->prog_if)
        return;

    Match->count++;
}

void usb_find_callback(const PCI_ret *device, void *context) {
    usb_controller_t *controller = static_cast<usb_controller_t *>(context);

    if (controller->found || device->PCI_status != PCI_STATUS_SUCCESS) 
        return;

    if (device->common.class_code != controller->class_code ||
        device->common.subclass != controller->subclass ||
        device->common.prog_if != controller->prog_if)
        return;

    for (u8 i = 0; i < Addr_count; i++) {
        if (device->Segment == segment[i] &&
            device->Bus == bus[i] && device->Device == Device[i] &&
            device->Function == function[i])
            return;

    }

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

    if (!controller->found) 
        return false;
    

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


usb_device usb_controller::get_device(u8 port, TimerSource &timer) {
    usb_device device = {0};
    pci_address_t pci_address = controller.pci_address;
    u32 segment = (u32)pci_address.segment;
    u32 bus = (u32)pci_address.bus;
    u32 device = (u32)pci_address.device;
    u32 function = (u32)pci_address.function

    int rests = reset_port(port, timer);
    if (rests != RESET_SUCCESS) {
        printf("%s at %u:%u:%u.%u: resetting port %b failed, error_code(%u): %s\n", vendor, 
            segment, 
            bus, 
            device, 
            function, port, rests, codes[rests-1]);
        device.discovered = false;
        return device;
    }




}

void usb_controller::construct_controller() {

    // controller discovery
    bool dists = discover_controller(&controller, class_code, subclass, prog_if, vendor); // find the controller's pci_address
    if (!dists) {
        status = 1;
        return; // return if there was a discovery failure
    }
    MapController(&controller, type); // map the controller to the virtual MMIO space if needed


    // controller pci requirements
    if (pci_enable_device(controller.pci_address) != PCI_STATUS_SUCCESS) { // enable the pci device
        printf("%s: failed to enable pci device", vendor); // print a message if error
        status = 1;
        return; // abort if failure
    }

    if (pci_enable_bus_mastering(controller.pci_address) != PCI_STATUS_SUCCESS) { // enable pci bus mastering
        printf("%s: failed to enable bus mastering", vendor); // print a message if error
        status = 1;
        return; // abort if failure
    }

    
    Timer *timer = reinterpret_cast<Timer *>(timeradd); // get a pointer to the global timer class
    status = controller_init(*timer->Src);

    if (!status) {
        ports_count = enumrate_ports();

        for (int i = 0; i < ports_count; i++) {
            if (ports[i].connected) {
                ports[i].device = new usb_device{};
                usb_device device = *ports[i].device;
                device = get_device(ports[i].port_number, timer);
                *ports[i].device = device;
            }
        }
        
    }

    segment[Addr_count] = controller.pci_address.segment;
    bus[Addr_count] = controller.pci_address.bus;
    Device[Addr_count] = controller.pci_address.device;
    function[Addr_count] = controller.pci_address.function;

    Addr_count++;
}



GlobalUSB::GlobalUSB() {
    class_code = 0x0C;
    subclass = 0x03;
    
    for (prog_if = 0x00; prog_if < 0x40; prog_if += 0x10) {
        USBCount Match = {
            class_code, subclass, prog_if, 0
        };

        pci_enumerate(usb_count_controllers, &Match);

        if (Match.count == 0)
            continue;

        segment = new u16[Match.count];
        bus = new u8[Match.count];
        Device = new u8[Match.count];
        function = new u8[Match.count];

        Addr_count = 0;

        switch (prog_if) {
            case 0x00:
                uhci_controllers = new uhci_controller[Match.count];
                break;

            case 0x10:
                ohci_controllers = new ohci_controller[Match.count];
                break;

            case 0x20:
                ehci_controllers = new ehci_controller[Match.count];
                break;

            case 0x30:
                xhci_controllers = new xhci_controller[Match.count];
                break;
        }

        delete[] segment;
        delete[] bus;
        delete[] Device;
        delete[] function;
    }
}