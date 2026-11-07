#include <assert.h>
#include <stdio.h>
#include "drivers/usb/usb.hpp"

int main() {
    usb_endpoint endpoints[3] = {};
    endpoints[0].address = 0x83;
    endpoints[0].transfer_type = 3; // An interrupt endpoint is not a bulk endpoint.
    endpoints[0].max_packet_size = 8;
    endpoints[1].address = 0x02;
    endpoints[1].transfer_type = 2;
    endpoints[1].max_packet_size = 64;
    endpoints[2].address = 0x81;
    endpoints[2].transfer_type = 2;
    endpoints[2].max_packet_size = 64;

    usb_interface interface = {};
    interface.endpoints = endpoints;
    interface.endpoint_count = 3;

    u8 bulk_in = 0xff, bulk_out = 0xff;
    assert(usb_find_bulk_endpoints(interface, bulk_in, bulk_out));
    assert(bulk_in == 2 && bulk_out == 1); // Array indices, not endpoint numbers.

    bulk_in = bulk_out = 0xff;
    endpoints[2].transfer_type = 3;
    assert(!usb_find_bulk_endpoints(interface, bulk_in, bulk_out));
    assert(bulk_in == 0xff && bulk_out == 0xff);
    endpoints[2].transfer_type = 2;
    endpoints[1].max_packet_size = 0;
    assert(!usb_find_bulk_endpoints(interface, bulk_in, bulk_out));
    endpoints[1].max_packet_size = 64;
    interface.descriptor.bAlternateSetting = 1;
    assert(!usb_find_bulk_endpoints(interface, bulk_in, bulk_out));
    interface.descriptor.bAlternateSetting = 0;
    interface.endpoints = nullptr;
    assert(!usb_find_bulk_endpoints(interface, bulk_in, bulk_out));

    puts("USB bulk endpoint selection tests passed");
}
