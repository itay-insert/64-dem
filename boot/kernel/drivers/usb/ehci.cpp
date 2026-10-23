#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/ehci.hpp"



ehci_controller::ehci_controller() { // initialize the ehci_controller fields
    memcpy(vendor, "EHCI", sizeof(vendor));
    controller = {0};
    class_code = 0x0C;
    subclass = 0x03;
    prog_if = 0x20;
    type = MMIO;
    construct_controller();
}