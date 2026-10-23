#pragma once
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"

#define MMIO 0
#define IO 1

typedef struct {
    bool found;
    pci_address_t pci_address;
    pci_bar_t bar;
    u64 io_base;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
} usb_controller_t;

class usb_controller {
    public:
        char vendor[5];
        usb_controller_t controller;
        u8 class_code;
        u8 subclass;
        u8 prog_if;
        u8 type;
        int status;
        bool discover_controller(usb_controller_t *controller, u8 class_code, u8 subclass, u8 prog_if, const char *str);
        void MapController(usb_controller_t *controller, u8 type);
        void construct_controller(void);
        virtual int controller_init(TimerSource &timer) = 0;
        

};


void usb_find_callback(const PCI_ret *device, void *context);