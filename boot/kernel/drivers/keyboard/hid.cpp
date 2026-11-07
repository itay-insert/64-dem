#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "drivers/display/vga.h"
#include "drivers/keyboard/hid.hpp"
#include "drivers/usb/usb.hpp"

extern u64 keyboard_usb_address;
extern u64 usb_addr;

usb_controller *controller = nullptr;
u8 port_index = 0;
u8 interface_index = 0;
u8 endpoint_index = 0;

void HIDK::init() {
    GlobalUSB *USB = (GlobalUSB *)usb_addr;

    u8 type = keyboard_usb_address >> 32;
    u8 index = (keyboard_usb_address >> 24) & 0xff;

    switch (type) {
        case 0:
            controller = &USB->uhci_controllers[index];
            break;

        case 1:
            controller = &USB->ohci_controllers[index];
            break;

        case 2:
            controller = &USB->ehci_controllers[index];
            break;
            
        case 3: 
            controller = &USB->xhci_controllers[index];
            break;

        default:
            return; // no valic controller_type
    }


    port_index = (keyboard_usb_address >> 16) & 0xff;
    interface_index = (keyboard_usb_address >> 8) & 0xff;
    endpoint_index = keyboard_usb_address & 0xff;


}


extern "C" void usb_keyboard_handler(void) {
    
}