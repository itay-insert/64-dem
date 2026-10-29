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

#define CLEAR_PE ~(PE | UHCI_W1C_MASK | UHCI_WZ_MASK)


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

typedef struct uhci_td uhci_td;

struct alignas(16) uhci_td {
    u32 link;
    u32 status;
    u32 token;
    u32 buffer;


    u32 software[4];
};

static_assert(sizeof(uhci_td) == 16);

typedef struct uhci_qh uhci_qh;

struct alignas(16) uhci_qh {
    u32 head_link;
    u32 element_link;
    u32 reserved[2];
};

static_assert(sizeof(uhci_qh) == 16);

namespace uhci {
    namespace pid {
        constexpr u8 setup = 0x2D;
        constexpr u8 in    = 0x69;
        constexpr u8 out   = 0xE1;
    }

    namespace td {
        constexpr u32 active       = 1u << 23;
        constexpr u32 interrupt   = 1u << 24;
        constexpr u32 low_speed   = 1u << 26;
        constexpr u32 error_count = 3u << 27;

        // Length is encoded as (length - 1).
        // A zero-length transaction is encoded as 0x7FF.
        constexpr u32 encode_length(u32 length) {
            return length == 0 ? 0x7FFu : length - 1;
        }
    }
}

constexpr u32 UHCI_LINK_TERMINATE = 1u;
constexpr u32 UHCI_LINK_QH        = 2u;
constexpr u32 UHCI_LINK_DEPTH     = 4u;

constexpr u32 TD_ACTIVE      = 1u << 23;
constexpr u32 TD_IOC         = 1u << 24;
constexpr u32 TD_LOW_SPEED   = 1u << 26;
constexpr u32 TD_ERROR_COUNT = 3u << 27;
constexpr u32 TD_SPD         = 1u << 29;

constexpr u32 TD_ACTLEN_INIT = 0x7FFu;

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
    frame_list = (u32 *)ret.virtual_address;

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
    wait_ms(timer, 50);

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

int uhci_controller::disable_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > 2)
        return INVALID_PORT;

    u16 reg = (u16)(controller.io_base + 0x10 + ((port - 1) << 1));
    u16 status = inw(reg);

    if (status == 0xFFFF)
        return INVALID_STATUS;

    status &= CLEAR_PE;
    

    outw(reg, status);

    for (int i = 0; i < 100; i++) { // poll the status with 100 ms timeout
        status = inw(reg);
        if (!(status & PE)) 
            break;
        wait_ms(timer, 1);
    }

    return (status & PE) ? RESET_FAILED : RESET_SUCCESS;

}

enum class uhci_transfer_result {
    success,
    timeout,
    transfer_error
};

uhci_transfer_result wait_for_descriptor(volatile uhci_td *setup_td, volatile uhci_td *data_td, volatile uhci_td *status_td, 
TimerSource &timer, u32 &received_bytes) {
    received_bytes = 0;
    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        u32 setup_status = setup_td->status;
        u32 data_status = data_td->status;
        u32 status_status = status_td->status;

         // If a TD has stopped and reports an error, fail.
        if ((!(setup_status & uhci::td_status::active) &&
              (setup_status & uhci::td_status::error_mask)) ||
            (!(data_status & uhci::td_status::active) &&
              (data_status & uhci::td_status::error_mask)) ||
            (!(status_status & uhci::td_status::active) &&
              (status_status & uhci::td_status::error_mask))) {
            return uhci_transfer_result::transfer_error;
        }

        // All three stages must finish.
        if (!(setup_status  & uhci::td_status::active) &&
            !(data_status   & uhci::td_status::active) &&
            !(status_status & uhci::td_status::active)) {

            // UHCI encodes actual length as (length - 1).
            // 0x7FF therefore represents zero bytes.
            received_bytes =
                ((data_status &
                  uhci::td_status::actual_length_mask) + 1) & 0x7FF;

            return uhci_transfer_result::success;
        }

        wait_ms(timer, 1);

    }

    return uhci_transfer_result::timeout;
}

usb_device_descriptor uhci_controller::identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};
    dma_ret alloc = allocate_dma(128);
    if (alloc.physical_address > 0xffffffff) {
        printf("UHCI: Setup packet address too big\n");
        return device_descriptor;
    }

    uhci_qh *setup_qh = (uhci_qh *)alloc.virtual_address;
    uhci_td *setup_td = (uhci_td *)(alloc.virtual_address + 16);
    uhci_td *data_td = (uhci_td *)(alloc.virtual_address + 16 + 32);
    uhci_td *status_td = (uhci_td *)(alloc.virtual_address + 16 + 64);
    usb_setup_packet *setup = (usb_setup_packet *)(alloc.virtual_address + 16 + 64 + 32);

    *setup = {
        0x80,
        0x06,
        0x0100,
        0x0000,
        8
    };

    setup_qh->head_link = 1;
    setup_qh->element_link = (u32)(alloc.physical_address + ((u64)setup_td - alloc.virtual_address));
    setup_qh->reserved[0] = 0;
    setup_qh->reserved[1] = 0;

    setup_td->link = (u32)(alloc.physical_address + ((u64)data_td - alloc.virtual_address)) | UHCI_LINK_DEPTH;
    setup_td->status = uhci::td::active | TD_ERROR_COUNT;
    setup_td->token =
        0x2D |             // SETUP PID
        (0u << 8) |        // Address 0
        (0u << 15) |       // Endpoint 0
        (0u << 19) |       // DATA0
        (7u << 21);
    setup_td->buffer = (u32)(alloc.physical_address + ((u64)setup - alloc.virtual_address));


    data_td->link = (u32)(alloc.physical_address + ((u64)status_td - alloc.virtual_address)) | UHCI_LINK_DEPTh;
    data_td->status = TD_ACTIVE |
    TD_ERROR_COUNT |
    TD_ACTLEN_INIT;            // Request interrupt on completion

    data_td->token = 
         0x69 |             // IN PID
        (0u << 8) |        // Address 0
        (0u << 15) |       // Endpoint 0
        (1u << 19) |       // DATA1
        (7u << 21);        // Up to eight bytes
    data_td->buffer = (u32)(alloc.physical_address + ((u64)setup + 8 - alloc.virtual_address));


    status_td->link = UHCI_LINK_TERMINATE;
    status_td->status =
    TD_ACTIVE |
    TD_ERROR_COUNT |
    TD_ACTLEN_INIT |
    TD_IOC;            // Request interrupt on completion

    status_td->token =
        0xE1 |             // OUT PID
        (0u << 8) |        // Address 0
        (0u << 15) |       // Endpoint 0
        (1u << 19) |       // DATA1
        (0x7FFu << 21);    // Zero-length transaction

    status_td->buffer = 0; // Unused for zero-length status

    u32 qh_phys = (u32)alloc.physical_address;

    for (u32 i = 0; i < 1024; i++) {
        frame_list[i] = qh_phys | 0x2u;
    }

    u32 recieved = 0;

    auto result = wait_for_descriptor(setup_td, data_td, status_td, timer, recieved);
    if (result != uhci_transfer_result::success) {
        printf("UHCI: device descriptor transfer error\n");
        return device_descriptor;
    }

    usb_device_descriptor *desc = (usb_device_descriptor *)((u64)setup + 8);

    if (desc->bLength == 0x12 && desc->bDescriptorType == 0x01) {
        usb_device_descriptor *device_ptr = &device_descriptor;
        memcpy(device_ptr, desc, 8);

    }

    free_dma(alloc.virtual_address);
    
    return device_descriptor;
}