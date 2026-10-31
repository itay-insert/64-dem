#pragma once
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"


#define OUT 0x00
#define IN 0x80
#define GET_DESCRIPTOR 0x06

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

typedef struct {
    u8 BmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} usb_setup_packet;

typedef struct {
    bool found;
    bool needs_reinit;
    dma_ret quarantined_dma;
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
    bool discovered;

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
        usb_device get_device(u8 port, TimerSource &timer);
        virtual int controller_init(TimerSource &timer) = 0;
        virtual int enumrate_ports() = 0;
        virtual int reset_port(u8 port, TimerSource &timer) = 0;
        virtual usb_device_descriptor identify_device(u8 port, TimerSource &timer) = 0;

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