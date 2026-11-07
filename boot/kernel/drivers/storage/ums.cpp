#include "drivers/display/vga.h"
#include "drivers/usb/usb.hpp"

extern u64 usb_addr;

void ums_init() {
    GlobalUSB *USB = (GlobalUSB *)usb_addr;

    USB->
}