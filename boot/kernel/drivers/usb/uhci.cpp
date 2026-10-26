#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/uhci.hpp"
#include "drivers/display/vga.h"

#define UHCI_PORT_COUNT 2
#define UHCI_CCS (1 << 0) // current connect status

 #define PR (1 << 9) // port reset
#define PE (1 << 2) // port enable

#define UHCI_W1C_MASK 0x0A0A
#define UHCI_WZ_MASK  0xE000

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

uhci_controller::uhci_controller() {
    memcpy(vendor, "UHCI", sizeof(vendor));
    class_code = 0x0C;
    subclass = 0x03;
    prog_if = 0x00;
    type = IO;
    controller = {0};
    construct_controller();
}



int uhci_controller::controller_init(TimerSource &timer) {
    
    printf("UHCI I/O base: 0x%w\n", (u16)controller.io_base);

    u16 base = (u16)controller.io_base;

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

u16 uhci_read_port(u16 io_base, u8 port) {
    return inw(io_base + 0x10 + (port << 1));
}

int uhci_controller::enumrate_ports() {
    u16 io_base = (u16)controller.io_base;

    ports = new usb_port_descriptor[UHCI_PORT_COUNT]; // allocate the port descriptors
    if (ports == nullptr) // return -1 for allocation failure
        return -1;


    for (u8 port = 0; port < UHCI_PORT_COUNT; port++) {
        u16 status = uhci_read_port(io_base, port); // fill the port descriptors with port information
        ports[port].port_number = port + 1;
        ports[port].port_status = (u32)status;
        ports[port].connected = (status & UHCI_CCS); // an AND operation with UHCI_CCS to determine whether a port is connected or not
        ports[port].controller_type = UHCI;
    }


    return UHCI_PORT_COUNT; // return the number of ports
}

int uhci_controller::reset_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > 2)
        return INVALID_PORT;  // invalid port index


    u16 reg = (u16)(controller.io_base + 0x10 + ((port - 1) << 1));
    u16 status = inw(reg);
    if (!(status & UHCI_CCS))
        return NO_DEVICE; // no device is connected

    // assert reset
    outw(reg, (status & 0x1FF & ~UHCI_W1C_MASK) | PR);
    wait_ms(timer, 50)

    // deassert reset
    status = inw(reg);
    outw(reg, (status & ~(PR | UHCI_W1C_MASK | UHCI_WZ_MASK)) & ~PR);
    wait_ms(timer, 10);


    // read final port state
    status = inw(reg);


    if (status & PR)
        return RESET_STILL_ASSERTED; // reset still asserted


    if (!(status & PE)) { // enable the port if not enabled after reset
        status &= ~(UHCI_W1C_MASK | UHCI_WZ_MASK);
        status |= PE;
        outw(reg, status);
        wait_ms(timer, 1);
        status = inw(reg);
    }


    if ((status & UHCI_CCS) && (status & PE) && !(status & PR))
        return RESET_SUCCESS;
    else return RESET_FAILED;
}