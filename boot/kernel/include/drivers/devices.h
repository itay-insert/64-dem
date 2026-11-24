#ifndef DEVICES_H
#define DEVICES_H

#include "uint_definitions.h"
#include "x86-64/io.h"
#include "drivers/pci/pci.h"


#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN 


#define SPKR 0
#define KEYBOARD 1
#define DISK 2
#define MOUSE 3
#define USB 4

typedef struct {
    u8 type;
    u64 io_base;
    u64 size;
    char *vendor;
    pci_address_t pci_address;
} io_device;


typedef struct {
    char *cap;
    char *controller_vendor;
    u8 port;
    u16 interface;
    u16 endpoint;
    pci_address_t *controller_address;
    io_device *controller;
    void *context;
} usb_interface;

    
KERNEL_EXTERN_C_END 

#endif