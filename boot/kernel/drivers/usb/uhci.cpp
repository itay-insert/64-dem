#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/usb.hpp"

#define UHCI_CMD_RS       0x0001
#define UHCI_CMD_HCRESET  0x0002
#define UHCI_CMD_GRESET   0x0004

#define UHCI_STS_HCH      0x0020


#define UHCI_FRAME_COUNT 1024
#define UHCI_LINK_TERMINATE 0x00000001

#define UHCI_CMD_RS 0x0001
#define UHCI_STS_HCH 0x0020

typedef enum {
    UHCI_USBCMD = 0x00,  // USB command
    UHCI_USBSTS = 0x02,  // USB status
    UHCI_USBINTR = 0x04, // USB Interrupt Enable
    UHCI_FRNUM = 0x06,   // USB Frame Number
    UHCI_FLBASEADD = 0x08, // Frame List Base Address
    UHCI_SOFMOD = 0x0C, // Start Of Frame modify
    UHCI_PORTSC1 = 0x10, // USB port 1 status and control
    UHCI_PORTSC2 = 0x12 // USB port 2 status and control
} UHCI_Regs;


usb_controller_t uhci_controller = {0}; 

static void uhci_find_callback(const PCI_ret *device, void *context) {
    uhci_controller_t *controller = 
        static_cast<uhci_controller_t *>(context);

    if (controller->found ||
        device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != 0x0C ||
        device->common.subclass != 0x03 ||
        device->common.prog_if != 0x00)
        return;

    controller->pci_address = (pci_address_t) {
        .segment  = device->Segment,
        .bus      = device->Bus,
        .device   = device->Device,
        .function = device->Function,
    };

    controller->found = true;
}

bool uhci_discover(usb_controller_t *controller) {
    *controller = {};

    controller->class_code = 0x0C;
    controller->subclass = 0x03;
    controller->prog_if = 0x00;

    pci_enumerate(usb_find_callback, controller);

    if (!controller->found) {
        printf("UHCI: no controller found!\n");
        return false;
    }

    printf("UHCI found at %u:%u:%u.%u\n",
        (unsigned int)controller->pci_address.segment,
        (unsigned int)controller->pci_address.bus,
        (unsigned int)controller->pci_address.device,
        (unsigned int)controller->pci_address.function);

    return true;
}

int UHCI_init(TimerSource &timer) {
    uhci_controller_t *controller = &uhci_controller; 
    
    if (!uhci_discover(controller))  // discover UHCI
        return 1;

    if (pci_enable_device(controller->pci_address) != PCI_STATUS_SUCCESS) { // enable the device in pci
        printf("UHCI: failed to enable pci device\n");
        return 1;
    }


    if (pci_enable_bus_mastering(controller->pci_address) != PCI_STATUS_SUCCESS) { // enable bus mastering
        printf("UHCI: failed to enable device mastering\n");
        return 1;
    }

    // read I/O address from BAR4
    if (pci_read_bar(controller->pci_address, 4, 0, &controller->bar) != PCI_STATUS_SUCCESS) {
        printf("UHCI: failed to read BAR4\n"); 
        return 1;
    }

    if (controller->bar.type != PCI_BAR_IO || controller->bar.address == 0 
        || controller->bar.address > 0xFFFF) {  // detect invalid I/O address
            printf("UHCI: invalid I/O BAR4");
            return 1;
    }

    controller->io_base = (u16)controller->bar.address;

    printf("UHCI I/O base: 0x%w\n", (u16)controller->io_base);

    u16 base = (u16)controller->io_base;

    outw(base+UHCI_USBCMD, 0);   // send command 0 to USBCMD to halt the controller

    bool halted = false;

    u64 start = timer.read();
    u64 duration = timer.read_freq() / 100;

    while (timer.read() - start < duration) {
        if (inw(base+UHCI_USBSTS) & UHCI_STS_HCH) {   // 10 ms timeout for halting
            halted = true;
            break;
        }
    }


    if (!halted) {
        printf("UHCI: USB failed to halt\n");
        return 1;
    }

    outw(base+UHCI_USBCMD, UHCI_CMD_HCRESET);  // send command UCHI_CMD_HCRESET to USBCMD
    // to reset the controller

    bool reset = false;

    start = timer.read();

    while (timer.read() - start < duration) {
        if (!(inw(base+UHCI_USBCMD) & UHCI_CMD_HCRESET)) { // 10 ms timeout for reset
            reset = true;
            break;
        }
    }

    if (!reset) {
        printf("UHCI: controller failed to reset\n");
        return 1;
    }

    outw(base + UHCI_USBSTS, 0x003F); // Clear status bits: writing 0x003F clears 
    // the 6 defined status bits
    
    outw(base+UHCI_USBINTR, 0); // disable USB interrupts

    outw(base+UHCI_FRNUM, 0); // reset the Frame Number

    outw(base+UHCI_SOFMOD, 64); // set the Start Of Frame value to 64

    dma_ret ret = allocate_dma(4096); // allocate the 4kb Frame List
    if (ret.status != 0) {
        printf("UHCI: Frame List allocation failed\n");  // handle allocation failure 
        return 1;
    }
    u32 *frame_list = (u32 *)ret.virtual_address;

    for (int i = 0; i < UHCI_FRAME_COUNT; i++)
        frame_list[i] = UHCI_LINK_TERMINATE; // fill all the frame list entries as disabled

    u64 frame_phys = ret.physical_address;

    if (frame_phys > 0xFFFFFFFF) {
        printf("UHCI: Frame List Address too big!\n");
        return 1;
    }


    outl(base+UHCI_FLBASEADD, (u32)frame_phys); // pass the frame list 
    // physical address to the FLBASEADD register

    outw(base+UHCI_USBCMD, UHCI_CMD_RS); // send the UHCI_CMD_RS command to USBCMD to
    // start the controller

    bool running = false;

    start = timer.read();

    while (timer.read() - start < duration) {
        if (!(inw(base+UHCI_USBSTS) & UHCI_STS_HCH)) { // 10 ms timeout for starting the controller
            running = true;
            break;
        }
    }

    if (!running) {
        printf("UHCI: failed to start the controller\n");
        return 1;
    }


    printf("UHCI: controller successfully initialized and running\n");
    return 0;
}

