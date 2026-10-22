#pragma once
#include "drivers/pci/pci.h"

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
        char vendor[4];

}