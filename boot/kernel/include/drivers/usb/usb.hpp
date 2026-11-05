#pragma once
#include "drivers/pci/pci.h"
#include "drivers/devdef.h"
#include "drivers/timers/timer.hpp"


#define NEEDS_REINIT 0
#define LINK_ISSUE 1
#define ADDRESS_DEVICE_TIMEOUT 2
#define DISABLE_SLOT_ERROR 3
#define GET_DESC_TIMEOUT 4
#define RESTORATION_FAILURE 5
#define EVAL_CONTEXT_TIMEOUT 6


#define OUT 0x00
#define IN 0x80
#define GET_DESCRIPTOR 0x06

#define MMIO 0
#define IO 1

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

extern const char *codes[];

typedef struct {
    int max;
    dma_ret *dma;
    u16 *error_code;
} dma_cache;


typedef struct {
    int max;
    dma_cache *cache;
} quar_dma;

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
    quar_dma cached;
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
    u8  bLength;             // 0: 9
    u8  bDescriptorType;     // 1: 0x02
    u16 wTotalLength;        // 2: Total configuration tree length
    u8  bNumInterfaces;      // 4: Number of interfaces
    u8  bConfigurationValue; // 5: Value for SET_CONFIGURATION
    u8  iConfiguration;      // 6: String descriptor index
    u8  bmAttributes;        // 7: Configuration attributes
    u8  bMaxPower;           // 8: Maximum power consumption
} __attribute__((packed)) configuration_descriptor;

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} __attribute__((packed)) usb_interface_descriptor;



typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor;

typedef struct {
    u8 address;
    u8 transfer_type;
    u16 max_packet_size;
    u8 interval;

    u8 data_toggle;

    u8 interface_number;
    u8 alternate_setting;
    usb_endpoint_descriptor descriptor;
} usb_endpoint;

typedef struct {
    usb_interface_descriptor descriptor;
    usb_endpoint *endpoints;
    u8 endpoint_count;
} usb_interface;



typedef struct {
    u8 port_number;
    u8 address;
    u8 speed;

    u16 vendor_id;
    u16 product_id;

    bool discovered;

    usb_controller_t *controller;
    usb_device_descriptor descriptor;
    configuration_descriptor config;
    usb_interface *interfaces;
    u16 usb_interface_count;
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
        virtual int disable_port(u8 port, TimerSource &timer) = 0;
        virtual usb_device_descriptor identify_device(u8 port, TimerSource &timer) = 0;
        virtual usb_device set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port) = 0;
        virtual void *usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port) = 0;
        virtual int configure_endpoint(usb_endpoint_descriptor endpoint, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer, u8 port) = 0;
        configuration_descriptor get_config(u8 address, TimerSource &timer, u32 maxpacketsize, u8 port);

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
