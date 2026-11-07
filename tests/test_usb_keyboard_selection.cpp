#include <assert.h>
#include <stdio.h>
#include "drivers/usb/usb.hpp"
int main() {
    usb_endpoint endpoints[2] = {};
    endpoints[0].address = 0x81;
    endpoints[1].address = 0x82;
    for (auto &ep : endpoints) { ep.transfer_type = 3; ep.max_packet_size = 8; }
    usb_interface interfaces[2] = {};
    for (u8 i = 0; i < 2; ++i) {
        interfaces[i].descriptor.bInterfaceClass = 3;
        interfaces[i].descriptor.bInterfaceSubClass = 1;
        interfaces[i].descriptor.bInterfaceProtocol = 1;
        interfaces[i].descriptor.bInterfaceNumber = 4 + i;
        interfaces[i].endpoint_count = 1;
        interfaces[i].endpoints = &endpoints[i];
    }
    usb_device device = {};
    device.interfaces = interfaces;
    device.usb_interface_count = 2;
    u8 iface = 0xff, endpoint = 0xff;
    assert(!usb_find_boot_keyboard(device, iface, endpoint)); // Failed enumeration.
    device.discovered = true;
    assert(usb_find_boot_keyboard(device, iface, endpoint));
    assert(iface == 0 && endpoint == 0); // Primary survives a second match.
    interfaces[0].descriptor.bInterfaceProtocol = 2; // Mouse on first interface.
    assert(usb_find_boot_keyboard(device, iface, endpoint) && iface == 1);
    interfaces[1].descriptor.bAlternateSetting = 1;
    assert(!usb_find_boot_keyboard(device, iface, endpoint));
    interfaces[1].descriptor.bAlternateSetting = 0;
    endpoints[1].max_packet_size = 4;
    assert(!usb_find_boot_keyboard(device, iface, endpoint));
    endpoints[1].max_packet_size = 8;
    endpoints[1].address = 2; // OUT is not an input endpoint.
    assert(!usb_find_boot_keyboard(device, iface, endpoint));
    puts("USB boot keyboard selection tests passed");
}
