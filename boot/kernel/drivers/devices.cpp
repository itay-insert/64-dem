#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/devices.h"

#define SPKR_CNL2 0x42
#define SPKR_CMD 0x43
#define PC_SPKR 0x61

io_device *devices = nullptr;
usb_interface *interfaces = nullptr;

int devices_count = 0;
int usb_devices = 0;


void increase_iodevices(io_device device) {
    if (devices == nullptr || devices_count == 0) {
       devices = new io_devices[1];
       *devices = device;
       devices_count = 1;
       return;
    }


    u64 old_size = devices_count * sizeof(io_device);
    u64 new_size = old_size + sizeof(io_device);
    devices = (io_device *)krealloc(devices, old_size, new_size);
    devices[devices_count] = device;
    devices_count++;

}

