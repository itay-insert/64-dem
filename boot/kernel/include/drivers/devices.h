#ifndef DEVICES_H
#define DEVICES_H

#include "uint_definitions.h"
#include "x86-64/io.h"
#include "drivers/pci/pci.h"


#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN 


#define DEVICE_TYPE_SPKR 0
#define DEVICE_TYPE_KEYBOARD 1
#define DEVICE_TYPE_DISK 2
#define DEVICE_TYPE_MOUSE 3
#define DEVICE_TYPE_USB 4
#define DEVICE_TYPE_TIMER 5
#define DEVICE_TYPE_CMOS 6
#define DEVICE_TYPE_PCI 7

typedef struct {
    u8 type;
    u64 io_base;
    u64 size;
    const char *vendor;
    pci_address_t pci_address;
    bool io;
    bool pci;
} io_device;


typedef struct {
    const char *cap;
    u8 type;
    u8 port;
    u16 interface;
    pci_address_t controller_address;
} usb_interface_endpoint;

    
void increase_iodevices(io_device device);
void increase_usbdevices(usb_interface_endpoint device);
void print_devices(void);

KERNEL_EXTERN_C_END 

#endif