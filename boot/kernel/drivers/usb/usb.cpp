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
#include "x86-64/memory/kmalloc.h"

#define config_case 0x02
#define interface_case 0x04
#define endpoint_case 0x05


const char *codes[] = {
    "NO_DEVICE",
    "RESET_STILL_ASSERTED", 
    "RESET_FAILED",  
    "INVALID_PORT", 
    "CONTROLLER_HALTED",
    "COMPANION_REQUIRED",
    "PORT_BUSY",
    "OVER_CURRENT_CONDITION", 
    "OLD_FLAG_DID_NOT_CLEAR",
    "INVALID_STATUS" 
};


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

u8 current_index = 0;

bool usb_controller::discover_controller(usb_controller_t *controller, u8 class_code, u8 subclass, u8 prog_if, const char *str) {
    *controller = {};

    controller->class_code = class_code;
    controller->subclass = subclass;
    controller->prog_if = prog_if;
    controller->cached.cache = NULL;
    controller->cached.max = 0;
    controller->index = current_index;

    current_index++;

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


configuration_descriptor usb_controller::get_config(u8 address, TimerSource &timer, u32 maxpacketsize, u8 port) {
    configuration_descriptor desc = {0};
    usb_setup_packet get_conf = {
        0x80,
        0x06,
        0x0200,
        0,
        9
    };

    void *buffer = usb_send_packet(get_conf, maxpacketsize, 9, address, timer, port);
    if (buffer == nullptr) {
        printf("USB: get_config error\n");
        return desc;
    }
    configuration_descriptor *config = (configuration_descriptor *)(u64)buffer;

    if (config->bLength == sizeof(configuration_descriptor) &&
        config->bDescriptorType == config_case &&
        config->wTotalLength >= sizeof(configuration_descriptor) &&
        config->wTotalLength <= 4096 &&
        config->bConfigurationValue != 0)
        desc = *config;
    else
        printf("USB: invalid configuration descriptor on port %u\n", port);
    kfree(buffer);
    return desc;
}


static inline u16 count_tree(u8 *tree, u16 max_length, u8 Case) {
    u16 count = 0;
    u16 offset = 0;

    while (offset < max_length) {
        if (max_length - offset < 2)
            return count;

        const u8 *entry = tree + offset;
        u8 length = entry[0];
        u8 type = entry[1];

        if (length < 2 || length > max_length - offset)
            return count;

        if (type == Case)
            count++;

        offset += length;
    }

    return count;
}

static inline bool fill_interfaces(u8 *tree, u16 max_length, usb_interface *interfaces, u16 interfaces_count) {
    u16 count = 0;
    u16 offset = 0;
    u16 i = 0;
    while (offset < max_length && i < interfaces_count) {
        if (max_length - offset < 2)
            return false;

        const u8 *entry = tree + offset;
        u8 length = entry[0];
        u8 type = entry[1];

        if (length < 2 || length > max_length - offset)
            return false;

        if (type == 0x04) {
            if (length < sizeof(usb_interface_descriptor))
                return false;
            memcpy(&interfaces[i].descriptor, entry, sizeof(usb_interface_descriptor));
            if (interfaces[i].descriptor.bNumEndpoints == 0) {
                interfaces[i].endpoints = nullptr;
                interfaces[i].endpoint_count = 0;
                offset += length;
                i++;
                continue;
            }


            interfaces[i].endpoints = new usb_endpoint[interfaces[i].descriptor.bNumEndpoints]{};
            if (!interfaces[i].endpoints)
                return false;
            interfaces[i].endpoint_count = interfaces[i].descriptor.bNumEndpoints;

            while (type != 0x05 && interfaces[i].endpoint_count) {
                offset += length;
                if (max_length - offset < 2)
                    return false;
                entry = tree + offset;
                length = entry[0];
                type = entry[1];
                if (length < 2 || length > max_length - offset)
                    return false;
                if (type == 0x04)
                    return false;
            }

            bool was_endpoint = false;
            for (u8 j = 0; j < interfaces[i].endpoint_count; j += was_endpoint) {
                if (type == 0x04)
                    return false;

                was_endpoint = (type == 0x05);

                if (type == 0x05) {
                    if (length < sizeof(usb_endpoint_descriptor))
                        return false;
                    memcpy(&interfaces[i].endpoints[j].descriptor, entry, sizeof(usb_endpoint_descriptor));

                    // SuperSpeed endpoint companions immediately follow their endpoint.
                    const u16 next_offset = offset + length;
                    if (max_length - next_offset >= 2 && tree[next_offset + 1] == 0x30) {
                        const u8 companion_length = tree[next_offset];
                        if (companion_length < sizeof(usb_ss_endpoint_companion_descriptor) ||
                            companion_length > max_length - next_offset)
                            return false;
                        memcpy(&interfaces[i].endpoints[j].ss_companion,
                               tree + next_offset,
                               sizeof(usb_ss_endpoint_companion_descriptor));
                        interfaces[i].endpoints[j].has_ss_companion = true;
                    }

                    interfaces[i].endpoints[j].address = interfaces[i].endpoints[j].descriptor.bEndpointAddress;
                    interfaces[i].endpoints[j].alternate_setting = interfaces[i].descriptor.bAlternateSetting;
                    interfaces[i].endpoints[j].data_toggle = 0;
                    interfaces[i].endpoints[j].interface_number = interfaces[i].descriptor.bInterfaceNumber;
                    interfaces[i].endpoints[j].interval = interfaces[i].endpoints[j].descriptor.bInterval;
                    interfaces[i].endpoints[j].max_packet_size = interfaces[i].endpoints[j].descriptor.wMaxPacketSize & 0x07ff;
                    interfaces[i].endpoints[j].transfer_type = interfaces[i].endpoints[j].descriptor.bmAttributes & 0x03;
                }

                offset += length;

                if (j + was_endpoint == interfaces[i].endpoint_count)
                    break;

                if (max_length - offset < 2)
                    return false;

                entry = tree + offset;
                length = entry[0];
                type = entry[1];

                if (length < 2 || length > max_length - offset)
                    return false;

            }
            i++;
            continue;
        }

        offset += length;
    }

    return i == interfaces_count;
}


usb_device usb_controller::get_device(u8 port, TimerSource &timer) {
    usb_device Device = {0};
    pci_address_t pci_address = controller.pci_address;
    u32 segment = (u32)pci_address.segment;
    u32 bus = (u32)pci_address.bus;
    u32 device = (u32)pci_address.device;
    u32 function = (u32)pci_address.function;

    int rests = reset_port(port, timer);
    if (rests != RESET_SUCCESS) {
        printf("%s at %u:%u:%u.%u: resetting port %b failed, error_code(%u): %s\n", vendor, 
            segment, 
            bus, 
            device, 
            function, port, rests,
            rests > 0 && rests <= INVALID_STATUS ? codes[rests-1] : "UNKNOWN");
        Device.discovered = false;
        return Device;
    }

    usb_device_descriptor desc = identify_device(port, timer);
    Device = set_address(desc, timer, port);
    if (!Device.discovered)
        return Device;

    Device.controller = &controller;
    Device.product_id = Device.descriptor.idProduct;
    Device.vendor_id = Device.descriptor.idVendor;
    Device.port_number = port;
    printf("USB.%s.%u:%u:%u.%u: device on port %u, vendor=0x%w product=0x%w\n",
           vendor, segment, bus, device, function, port,
           Device.descriptor.idVendor, Device.descriptor.idProduct);

    // PCI USB controller programming interface 0x30 identifies xHCI.
    const bool is_xhci = controller.class_code == 0x0c &&
                         controller.subclass == 0x03 &&
                         controller.prog_if == 0x30;
    const u32 ep0_mps = is_xhci && Device.descriptor.bMaxPacketSize0 == 9
                      ? 512u : (u32)Device.descriptor.bMaxPacketSize0;

    Device.config = get_config(Device.address, timer, ep0_mps, port);

    u16 total = Device.config.wTotalLength;

    if (total >= sizeof(configuration_descriptor)) {
        usb_setup_packet packet = {
            0x80,
            0x06,
            0x0200,
            0,
            total
        };

        u8 *tree = (u8 *)usb_send_packet(packet, ep0_mps, total, Device.address, timer, port);

        if (!tree) {
            printf("USB: tree failure\n");
            Device.discovered = false;
        } else {
            u16 interfaces_count = count_tree(tree, total, interface_case);

            if (interfaces_count == 0) {
                kfree(tree);
                Device.discovered = false;
                return Device;
            }

            Device.interfaces = new usb_interface[interfaces_count]{};
            Device.usb_interface_count = interfaces_count;

            bool result = Device.interfaces &&
                fill_interfaces(tree, total, Device.interfaces, Device.usb_interface_count);
            kfree(tree);
            if (!result) {
                printf("USB: fill interfaces error\n");
                if (Device.interfaces)
                    for (u16 i = 0; i < interfaces_count; ++i)
                        delete[] Device.interfaces[i].endpoints;
                delete[] Device.interfaces;
                Device.interfaces = nullptr;
                Device.usb_interface_count = 0;
                Device.discovered = false;
                return Device;
            }
            for (u16 i = 0; i < Device.usb_interface_count; i++) {
                u8 iclass = Device.interfaces[i].descriptor.bInterfaceClass;
                u8 isubclass = Device.interfaces[i].descriptor.bInterfaceSubClass;
                u8 iprotocol = Device.interfaces[i].descriptor.bInterfaceProtocol;
                printf("USB.%s port %u interface %u: class=0x%b subclass=0x%b protocol=0x%b\n",
                       vendor, port, Device.interfaces[i].descriptor.bInterfaceNumber,
                       iclass, isubclass, iprotocol);

                if (iclass == 0x03 && isubclass == 0x01) {
                    if (iprotocol == 0x01)
                        printf("USB.%s.%u:%u:%u.%u: HID keyboard found at the device in port 0x%b, interface 0x%w\n", vendor,
                            segment, bus, device, function, port, i);
                    else if (iprotocol == 0x02)
                        printf("USB.%s.%u:%u:%u.%u: HID mouse found at the device in port 0x%b, interface 0x%w\n", vendor,
                            segment, bus, device, function, port, i);

                } else if (iclass == 0x08)
                    printf("USB.%s.%u:%u:%u.%u: USB Mass Storage device found at the device in port 0x%b, interface 0x%w\n", vendor,
                        segment, bus, device, function, port, i);
            }
            // xHCI must accept the endpoint contexts before the device request.
            if (is_xhci && configure_endpoints(Device, Device.config.bConfigurationValue,
                                               ep0_mps, Device.address, timer) != 0) {
                Device.discovered = false;
                return Device;
            }

            usb_setup_packet set_configuration = {
                0x00,
                0x09,
                Device.config.bConfigurationValue,
                0,
                0
            };

            u64 code = (u64)usb_send_packet(set_configuration, ep0_mps, 0,
                                            Device.address, timer, port);

            if (code != 1) {
                printf("USB: set_configuration error\n");
                if (is_xhci)
                    controller.needs_reinit = true;
                Device.discovered = false;
                return Device;
            }

            if (!is_xhci && configure_endpoints(Device, Device.config.bConfigurationValue,
                                                ep0_mps, Device.address, timer) != 0)
                Device.discovered = false;
        }
    } else {
        Device.discovered = false;
    }


    return Device;

}



void usb_controller::scan_ports(TimerSource &timer) {
    for (int i = 0; i < ports_count; i++) {
        if (ports[i].connected) {
            usb_device enumerated = get_device(ports[i].port_number, timer);
            if (enumerated.discovered)
                ports[i].device = new usb_device(enumerated);
        }
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
        if (ports_count > 0)
            scan_ports(*timer->Src);
        else
            printf("%s: port enumeration failed (%d)\n", vendor, ports_count);
        
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
    
    // EHCI must release full/low-speed ports before its UHCI/OHCI companions
    // take their boot-time port snapshots.
    const u8 controller_order[] = {0x20, 0x00, 0x10, 0x30};
    for (u8 i = 0; i < sizeof(controller_order); ++i) {
        prog_if = controller_order[i];
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
        current_index = 0;

        switch (prog_if) {
            case 0x00:
                uhci_controllers = new uhci_controller[Match.count];
                uhci_count = Match.count;
                break;

            case 0x10:
                ohci_controllers = new ohci_controller[Match.count];
                ohci_count = Match.count;
                break;

            case 0x20:
                ehci_controllers = new ehci_controller[Match.count];
                ehci_count = Match.count;
                break;

            case 0x30:
                xhci_controllers = new xhci_controller[Match.count];
                xhci_count = Match.count;
                break;
        }

        delete[] segment;
        delete[] bus;
        delete[] Device;
        delete[] function;
    }
}
