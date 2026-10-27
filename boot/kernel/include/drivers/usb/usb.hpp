#pragma once
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"

#define MMIO 0
#define IO 1

#define UHCI 0
#define OHCI 1
#define EHCI 2
#define xHCI 3

// if reset return code > 0 == failure
#define RESET_SUCCESS 0
#define NO_DEVICE 1
#define RESET_STILL_ASSERTED 2
#define RESET_FAILED 3
#define INVALID_PORT 4
#define CONTROLLER_HALTED 5
#define COMPANION_REQUIRED 6
#define PORT_BUSY 7
#define OVER_CURRENT_CONDITION 8
#define OLD_FLAG_DID_NOT_CLEAR 9
#define INVALID_STATUS 10

typedef struct {
    bool found;
    pci_address_t pci_address;
    pci_bar_t bar;
    u64 io_base;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 index;
} usb_controller_t;

typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor;

typedef struct {
    u8 port_number;
    u8 address;
    u8 speed;

    u16 vendor_id;
    u16 product_id;

    usb_controller_t *controller;
    usb_device_descriptor descriptor;
} usb_device;


typedef struct {
    u8 port_number;
    bool connected;
    int controller_type;
    u32 port_status;
    usb_device *device;
} usb_port_descriptor;

class usb_controller {
    public:
        char vendor[5];
        usb_controller_t controller;
        u8 class_code;
        u8 subclass;
        u8 prog_if;
        u8 type;
        int status;
        usb_port_descriptor *ports;
        int ports_count;
        bool discover_controller(usb_controller_t *controller, u8 class_code, u8 subclass, u8 prog_if, const char *str);
        void MapController(usb_controller_t *controller, u8 type);
        void construct_controller(void);
        virtual int controller_init(TimerSource &timer) = 0;
        virtual int enumrate_ports() = 0;
        virtual int reset_port(u8 port, TimerSource &timer) = 0;

};


class GlobalUSB {
    public: 
        u8 class_code;
        u8 subclass;
        u8 prog_if;
        usb_controller *xhci_controllers;
        usb_controller *ehci_controllers;
        usb_controller *ohci_controllers;
        usb_controller *uhci_controllers;
        GlobalUSB();
        
};


void usb_find_callback(const PCI_ret *device, void *context);