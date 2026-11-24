#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/display/vga.h"
#include "drivers/devices.h"

#define SPKR_CNL2 0x42
#define SPKR_CMD 0x43
#define PC_SPKR 0x61

static io_device *io_devices = nullptr;
usb_interface_endpoint *interfaces = nullptr;

int devices_count = 0;
int usb_devices = 0;

extern bool ps2_status;

extern "C" void increase_iodevices(io_device device) {
    if (io_devices == nullptr || devices_count == 0) {
       io_devices = new io_device[1];
       *io_devices = device;
       devices_count = 1;
       return;
    }


    u64 old_size = devices_count * sizeof(io_device);
    u64 new_size = old_size + sizeof(io_device);
    io_devices = (io_device *)krealloc(io_devices, old_size, new_size);
    io_devices[devices_count] = device;
    devices_count++;

}



extern "C" void increase_usbdevices(usb_interface_endpoint device) {
    if (interfaces == nullptr || usb_devices == 0) {
        interfaces = new usb_interface_endpoint[1];
        *interfaces = device;
        usb_devices = 1;
        return;
    }


    u64 old_size = usb_devices * sizeof(usb_interface_endpoint);
    u64 new_size = old_size + sizeof(usb_interface_endpoint);
    interfaces = (usb_interface_endpoint *)krealloc(interfaces, old_size, new_size);
    interfaces[usb_devices] = device;
    usb_devices++;
}

const char *types[] = {
    "SPKR",
    "KEYBOARD",
    "DISK",
    "MOUSE",
    "USB",
    "TIMER",
    "CMOS",
    "PCI"
};


static inline void print_usb_device(usb_interface_endpoint *device) {
    printf("usb device at port 0x%b, interface 0x%w (%s) of type %s\n", device->port,
        device->interface, device->cap, types[device->type]);
}

extern "C" void print_devices(void) {
    printf("device PC speaker of type SPKR i/o ports:\n");
    printf("SPKR_CNL2 port 0x42\n");
    printf("SPKR_CMD port 0x43\n");
    printf("PC_SPKR port 0x61\n");


    printf("device RTC of type CMOS i/o ports:\n");
    printf("CMOS_INDEX port 0x70\n");
    printf("CMOS_DATA port 0x71\n");

    if (ps2_status) {
        printf("PS/2 controller i/o ports for keyboard and mouse:\n");
        printf("Data register: port 0x60\n");
        printf("Status register: port 0x64\n");
    }

    for (int i = 0; i < devices_count; i++) {
        if (io_devices[i].pci) {
            u32 segment = (u32)io_devices[i].pci_address.segment;
            u32 bus = (u32)io_devices[i].pci_address.bus;
            u32 device = (u32)io_devices[i].pci_address.device;
            u32 function = (u32)io_devices[i].pci_address.function;

            printf("pci device %u:%u:%u.%u (%s) of type %s\n", segment, bus, device, function, io_devices[i].vendor, types[io_devices[i].type]);

            if (!io_devices[i].io)
                printf("MMIO region: 0x%lx - 0x%lx\n", io_devices[i].io_base, (io_devices[i].io_base+io_devices[i].size));
            else
                printf("i/o port base: 0x%w\n", (u16)io_devices[i].io_base);

            if (io_devices[i].type == DEVICE_TYPE_USB) {
                for (int j = 0; j < usb_devices; j++) {
                    const pci_address_t &controller = interfaces[j].controller_address;
                    const pci_address_t &address = io_devices[i].pci_address;
                    if (controller.segment == address.segment && controller.bus == address.bus &&
                        controller.device == address.device && controller.function == address.function) {
                        print_usb_device(&interfaces[j]);
                    }


                }
            }

        } else {
            printf("device (%s) of type %s discovered\n", io_devices[i].vendor, types[io_devices[i].type]);
            if (!io_devices[i].io)
                printf("MMIO region: 0x%lx - 0x%lx\n", io_devices[i].io_base, (io_devices[i].io_base+io_devices[i].size));
            else
                printf("i/o port base: 0x%w\n", (u16)io_devices[i].io_base);

        }

    }
}