#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/uhci.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/kmalloc.h"

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
        constexpr u32 error_mask = 0x007E0000u;
        constexpr u32 actual_length_mask = 0x7FFu;
    

        // Length is encoded as (length - 1).
        // A zero-length transaction is encoded as 0x7FF.
        constexpr u32 encode_length(u32 length) {
            return length == 0 ? 0x7FFu : length - 1;
        }
    }
}

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

    states = new uhci_endpoint_state[UHCI_PORT_COUNT];

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

#define UHCI_TD_SPD (1u << 29)

static inline u32 uhci_actual_length(u32 status) {
    return ((status & uhci::td::actual_length_mask) + 1) & 0x7FF;
}

static inline u32 uhci_max_length(u32 token) {
    return (((token >> 21) & 0x7FF) + 1) & 0x7FF;
}

static inline bool uhci_td_failed(u32 status) {
    return !(status & uhci::td::active) &&
           (status & uhci::td::error_mask);
}

uhci_transfer_result wait_for_control_transfer(
    volatile uhci_qh *qh,
    volatile uhci_td *setup_td,
    volatile uhci_td *data_tds,
    u32 data_td_count,
    volatile uhci_td *status_td,
    u32 status_td_phys,
    TimerSource &timer,
    u32 &received_bytes
) {
    received_bytes = 0;

    bool short_packet = false;

    for (u32 elapsed = 0; elapsed < 100; elapsed++) {

        u32 setup_status = setup_td->status;

        if (uhci_td_failed(setup_status))
            return uhci_transfer_result::transfer_error;

        // Wait for SETUP to finish before processing data.
        if (setup_status & uhci::td::active) {
            wait_ms(timer, 1);
            continue;
        }

        // Process the data stage.
        if (!short_packet) {

            bool all_data_done = true;
            u32 total_received = 0;

            for (u32 i = 0; i < data_td_count; i++) {

                u32 td_status = data_tds[i].status;

                if (uhci_td_failed(td_status))
                    return uhci_transfer_result::transfer_error;

                // Stop at the first unfinished TD.
                if (td_status & uhci::td::active) {
                    all_data_done = false;
                    break;
                }

                u32 actual = uhci_actual_length(td_status);
                u32 requested = uhci_max_length(data_tds[i].token);

                total_received += actual;

                // Early short IN packet.
                if ((td_status & UHCI_TD_SPD) &&
                    actual < requested) {

                    received_bytes = total_received;

                    // Status TD was already initialized.
                    asm volatile("mfence" ::: "memory");

                    // Restart the QH at the status stage.
                    qh->element_link = status_td_phys;

                    asm volatile("mfence" ::: "memory");

                    short_packet = true;
                    break;
                }
            }

            if (!short_packet) {
                received_bytes = total_received;

                if (!all_data_done) {
                    wait_ms(timer, 1);
                    continue;
                }
            }
        }

        // Wait for the status stage.
        u32 status_status = status_td->status;

        if (uhci_td_failed(status_status))
            return uhci_transfer_result::transfer_error;

        if (!(status_status & uhci::td::active))
            return uhci_transfer_result::success;

        wait_ms(timer, 1);
    }

    return uhci_transfer_result::timeout;
}


bool wait_for_halted(u16 io_base, TimerSource &timer) {
    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        if (inw(io_base+UHCI_USBSTS) & UHCI_STS_HCH)
            return true;
        
        wait_ms(timer, 1);
    }

    return (inw(io_base+UHCI_USBSTS) & UHCI_STS_HCH) != 0;
}


bool wait_for_start(u16 io_base, TimerSource &timer) {
    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        if (!(inw(io_base+UHCI_USBSTS) & UHCI_STS_HCH))
            return true;

        wait_ms(timer, 1);
    }

    return (inw(io_base+UHCI_USBSTS) & UHCI_STS_HCH) == 0;
}

usb_device_descriptor uhci_controller::identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};

    u16 io_base = (u16)controller.io_base;
    dma_ret alloc = allocate_dma(128);
    if (alloc.status != 0) {
        printf("UHCI: setup packet allocation failed\n");
        return device_descriptor;
    }
    if (alloc.physical_address > 0xffffffff) {
        printf("UHCI: Setup packet address too big\n");
        free_dma(alloc.virtual_address);
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


    data_td->link = (u32)(alloc.physical_address + ((u64)status_td - alloc.virtual_address)) | UHCI_LINK_DEPTH;
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


    u16 old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u));

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: failed to halt controller\n");
        free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    u32 *backup = new u32[1024];

    for (u32 i = 0; i < 1024; i++) {
        backup[i] = frame_list[i];
        frame_list[i] = qh_phys | 0x2u;
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, (old_command | 1u));

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: failed to start controller\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        return device_descriptor;
    }


    u32 recieved = 0;

    auto result = wait_for_control_transfer(setup_qh, setup_td, data_td, 1, status_td,
                                            (u32)(alloc.physical_address + ((u64)status_td - alloc.virtual_address)), timer, recieved);
    if (result != uhci_transfer_result::success) {
        printf("UHCI: device descriptor transfer error\n");
        old_command = inw(io_base+UHCI_USBCMD);
        outw(io_base+UHCI_USBCMD, (old_command & ~1u));

        if (!wait_for_halted(io_base, timer)) {
            printf("UHCI: failed to halt controller\n");
            free_dma(alloc.virtual_address);
            delete[] backup;
            return device_descriptor;
        }

        for (u32 i = 0; i < 1024; i++) {
            frame_list[i] = backup[i];
        }
        asm volatile("mfence" ::: "memory");
        outw(io_base+UHCI_USBCMD, (old_command | 1u));

        if (!wait_for_start(io_base, timer)) {
            printf("UHCI: failed to start controller\n");
        }

        delete[] backup;

        free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    usb_device_descriptor *desc = (usb_device_descriptor *)((u64)setup + 8);

    if (desc->bLength == 0x12 && desc->bDescriptorType == 0x01) {
        usb_device_descriptor *device_ptr = &device_descriptor;
        memcpy(device_ptr, desc, 8);

    }


    old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u));  // halt the controller temporarily

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: failed to halt controller\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        return device_descriptor;
    }

    for (u32 i = 0; i < 1024; i++) {
        frame_list[i] = backup[i];
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, (old_command | 1u)); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: failed to start controller\n");
    }

    delete[] backup;
    free_dma(alloc.virtual_address);
    
    return device_descriptor;
}




#define USB_PID_SETUP  0x2D
#define USB_PID_IN     0x69
#define USB_PID_OUT    0xE1


#define USB_DATA0      0
#define USB_DATA1      1


#define USB_DIR_OUT    0x00
#define USB_DIR_IN     0x80

// Link pointer flags
#define UHCI_PTR_TERM  0x01
#define UHCI_PTR_QH    0x02
#define UHCI_PTR_DEPTH 0x04

// TD status flags
#define UHCI_TD_IOC    (1u << 24)
#define UHCI_TD_ACTIVE (1u << 23)
#define UHCI_TD_LS     (1u << 26)

#define UHCI_TD_CERR(n) ((u32)(n) << 27)

// Token fields
#define UHCI_TOKEN_ADDR(addr)  ((u32)(addr) << 8)
#define UHCI_TOKEN_EP(ep)      ((u32)(ep) << 15)
#define UHCI_TOKEN_TOGGLE(t)   ((u32)(t) << 19)

#define UHCI_TOKEN_MAXLEN(len) \
    ((u32)((len) == 0 ? 0x7FF : (len) - 1) << 21)


#define UHCI_TD_SPD (1u << 29)

static u8 next_usb_address = 1;

static inline u8 allocate_usb_address(void) {
    if (next_usb_address > 127) 
        return 0;  // no addresses left

    return next_usb_address++;
}


usb_device uhci_controller::set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port) {
    usb_device device = {0};

    u64 size = (sizeof(uhci_td) << 1) + sizeof(uhci_qh) + sizeof(usb_setup_packet);

    u16 io_base = (u16)controller.io_base;
    dma_ret alloc = allocate_dma(size);
    if (alloc.status != 0) {
        printf("UHCI: Set address allocation failed\n");
        device.discovered = false; // return discovery failure
        return device;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("UHCI: Set address physical address too big!\n");
        free_dma(alloc.virtual_address);
        device.discovered = false; // return discovery failure
        return device;
    }


    u64 qh_phys = alloc.physical_address;
    u64 setup_td_phys = qh_phys + sizeof(uhci_qh);
    u64 status_phys = setup_td_phys + sizeof(uhci_td);
    u64 setup_phys = status_phys + sizeof(uhci_td); 


    uhci_qh *qh = (uhci_qh *)alloc.virtual_address;
    uhci_td *setup_td = (uhci_td *)(alloc.virtual_address + sizeof(uhci_qh));
    uhci_td *status_td = (uhci_td *) ((u8 *)setup_td + sizeof(uhci_td));
    usb_setup_packet *setup = (usb_setup_packet *)((u8 *)status_td + sizeof(uhci_td));

    u8 address = allocate_usb_address();  // allocate the usb address

    *setup = {
        .BmRequestType = 0x00, // Host-to-device
        .bRequest = 0x05, // Set Address
        .wValue = address, // the assigned usb address
        .wIndex = 0,
        .wLength = 0
    };

    // initializing the qh
    *qh = {};

    qh->head_link = UHCI_PTR_TERM;
    qh->element_link = (u32)setup_td_phys;


    // initializing the setup_td
    *setup_td = {};

    setup_td->link = ((u32)status_phys) | UHCI_PTR_DEPTH;

    setup_td->status =
        UHCI_TD_ACTIVE |
        UHCI_TD_CERR(3);

    setup_td->token =
        USB_PID_SETUP |
        UHCI_TOKEN_ADDR(0) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA0) |
        UHCI_TOKEN_MAXLEN(8);

    setup_td->buffer = (u32)setup_phys;


    // initializing the status_td
    *status_td = {};


    status_td->link = UHCI_LINK_TERMINATE;

    status_td->status =
        UHCI_TD_ACTIVE |
        UHCI_TD_CERR(3);

    status_td->token = 
        USB_PID_IN |
        UHCI_TOKEN_ADDR(0) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA1) |
        UHCI_TOKEN_MAXLEN(0);

    status_td->buffer = 0;
        

    u16 old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller temporarily

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: controller failed to halt\n");
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    u32 *backup = new u32[1024];

    for (int i = 0; i < 1024; i++) {
        backup[i] = frame_list[i];
        frame_list[i] = qh_phys | 0x02u;
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: controller failed to start\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        return device;
    }

    u32 recieved = 0;
    auto result = wait_for_control_transfer(qh, setup_td, nullptr, 0, status_td,
                                            (u32)status_phys, timer, recieved);
    if (result != uhci_transfer_result::success) {
        printf("UHCI: set address error\n");
        old_command = inw(io_base+UHCI_USBCMD);
        outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller

        if (!wait_for_halted(io_base, timer)) {
            printf("UHCI: controller failed to halt\n");
            free_dma(alloc.virtual_address);
            delete[] backup;
            device.discovered = false;
            return device;
        }

        for (int i = 0; i < 1024; i++) 
            frame_list[i] = backup[i];
        
        asm volatile("mfence" ::: "memory");

        outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

        if (!wait_for_start(io_base, timer)) {
            printf("UHCI: controller failed to start\n");
        }
        next_usb_address = address;

        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        return device;
    }

    device.address = address; // save the usb address

    wait_ms(timer, 2);

    old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller temporarily

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: controller failed to halt\n");
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    for (int i = 0; i < 1024; i++) 
        frame_list[i] = backup[i];

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: controller failed to start\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        return device;
    }

    free_dma(alloc.virtual_address);

    size = sizeof(uhci_qh) + (sizeof(uhci_td) * 5) + 8 + 18;

    alloc = allocate_dma(size);
    if (alloc.status != 0) {
        printf("UHCI: GET DESCRIPTOR allocation failed\n");
        device.discovered = false; // return discovery failure
        return device;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("UHCI: GET DESCRIPTOR physical address too big!\n");
        free_dma(alloc.virtual_address);
        device.discovered = false; // return discovery failure
        return device;
    }

    qh_phys = alloc.physical_address;
    setup_td_phys = alloc.physical_address + sizeof(uhci_qh);
    u64 data_td_phys = setup_td_phys + sizeof(uhci_td);
    status_phys = data_td_phys + (sizeof(uhci_td) * 3);
    setup_phys = status_phys + sizeof(uhci_td);
    u64 desc_phys = setup_phys + 8;

    qh = (uhci_qh *)alloc.virtual_address;
    setup_td = (uhci_td *)(alloc.virtual_address + sizeof(uhci_qh));
    uhci_td *data_tds = (uhci_td *)((u8 *)setup_td + sizeof(uhci_td));
    status_td = (uhci_td *)((u8 *)data_tds + (sizeof(uhci_td) * 3));
    setup = (usb_setup_packet *)((u8 *)status_td + sizeof(uhci_td));

    *setup = {
        .BmRequestType = 0x80, // device to host
        .bRequest = 0x06,
        .wValue = 0x0100,
        .wIndex = 0,
        .wLength = 18
    };


    u32 common_status =
        uhci::td::active |
        uhci::td::error_count;

    if (uhci_read_port(io_base, port - 1) & (1u << 8))
        common_status |= uhci::td::low_speed;


    // SETUP TD
    setup_td->link = data_td_phys | UHCI_PTR_DEPTH;

    setup_td->status = common_status;

    setup_td->token =
        USB_PID_SETUP |
        UHCI_TOKEN_ADDR(address) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA0) |
        UHCI_TOKEN_MAXLEN(8);

    setup_td->buffer = (u32)setup_phys;

    u8 max_packet_size = desc.bMaxPacketSize0;
    u32 data_td_count = 3;

    // DATA TDs
    u32 offset = 0;
    u8 toggle = USB_DATA1;

    for (u32 i = 0; i < data_td_count; i++) {
        u32 remaining = 18 - offset;
        u32 length = remaining < max_packet_size
                  ? remaining
                : max_packet_size;

        bool last = (i == data_td_count - 1);

        data_tds[i].link =
            last
                ? status_phys | UHCI_PTR_DEPTH
                : data_td_phys +
                (i + 1) * sizeof(uhci_td) |
                UHCI_PTR_DEPTH;

        data_tds[i].status =
            common_status | (last ? 0 : UHCI_TD_SPD);

        data_tds[i].token =
            USB_PID_IN |
            UHCI_TOKEN_ADDR(address) |
            UHCI_TOKEN_EP(0) |
            UHCI_TOKEN_TOGGLE(toggle) |
            UHCI_TOKEN_MAXLEN(length);

        data_tds[i].buffer = desc_phys + offset;

        offset += length;
        toggle ^= 1;
    }


// STATUS TD
    status_td->link = UHCI_PTR_TERM;

    status_td->status =
        common_status |
        uhci::td::interrupt;

    status_td->token =
        USB_PID_OUT |
        UHCI_TOKEN_ADDR(address) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA1) |
        UHCI_TOKEN_MAXLEN(0);

    status_td->buffer = 0;


    // QH
    qh->head_link = UHCI_PTR_TERM;
    qh->element_link = (u32)setup_td_phys;

    old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller temporarily

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: controller failed to halt\n");
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    for (int i = 0; i < 1024; i++) 
        frame_list[i] = qh_phys | 0x2u;

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: controller failed to start\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        return device;
    }

    result = wait_for_control_transfer(qh, setup_td, data_tds, 3, status_td, status_phys, timer, recieved);
    if (result != uhci_transfer_result::success) {
        printf("UHCI: GET DESCRIPTOR error\n");
        old_command = inw(io_base+UHCI_USBCMD);
        outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller

        if (!wait_for_halted(io_base, timer)) {
            printf("UHCI: controller failed to halt\n");
            free_dma(alloc.virtual_address);
            delete[] backup;
            device.discovered = false;
            return device;
        }

        for (int i = 0; i < 1024; i++) 
            frame_list[i] = backup[i];
        
        asm volatile("mfence" ::: "memory");

        outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

        if (!wait_for_start(io_base, timer)) {
            printf("UHCI: controller failed to start\n");
        }

        int rests = reset_port(port, timer);
        if (rests != 0) {
            printf("UHCI: failed to reset port 0x%b\n", port);
        } else {
            next_usb_address = address;
        }

        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        device.address = 0;
        return device;
    }


    usb_device_descriptor *desc_ptr = (usb_device_descriptor *)((u8 *)setup + 8);
    if (desc_ptr->bDescriptorType == 0x01 && desc_ptr->bLength == 18) {
        memcpy(&desc, desc_ptr, 18);
    }

    old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u));
    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: failed to halt controller\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        device.discovered = false;
        return device;
    }

    for (u32 i = 0; i < 1024; i++) {
        frame_list[i] = backup[i];
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, (old_command | 1u)); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: failed to start controller\n");
    }

    delete[] backup;
    free_dma(alloc.virtual_address);
    
    device.discovered = true;
    device.descriptor = desc;
    return device;
}


void *uhci_controller::usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port) {
    u16 io_base = (u16)controller.io_base;
    u32 data_td_count = 0;

    if (maxpacketsize)
        data_td_count = (length + maxpacketsize-1) / maxpacketsize;

    u64 size = ((data_td_count + 2) * sizeof(uhci_td)) + sizeof(uhci_qh) + length + 8;
    
    dma_ret alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("UHCI: packet allocation failed\n");
        return nullptr;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("UHCI: packet physical address too big!\n");
        free_dma(alloc.virtual_address);
        return nullptr;
    }

    u32 qh_phys = (u32)alloc.physical_address;
    u32 setup_td_phys = qh_phys + sizeof(uhci_qh);
    u32 data_td_phys = setup_td_phys + sizeof(uhci_td);

    u32 status_td_phys = data_td_phys + (sizeof(uhci_td) * data_td_count);
    u32 setup_phys = status_td_phys + sizeof(uhci_td);
    u32 desc_phys = setup_phys + 8;

    uhci_qh *qh = (uhci_qh *)alloc.virtual_address;
    uhci_td *setup_td = (uhci_td *)((u8 *)qh + sizeof(uhci_qh));
    uhci_td *data_td = nullptr;
    if (data_td_count) 
        data_td = (uhci_td *)((u8 *)setup_td + sizeof(uhci_td));

    uhci_td *status_td = (uhci_td *)((u8 *)setup_td + sizeof(uhci_td) + (sizeof(uhci_td) * data_td_count));
        
    usb_setup_packet *setup = (usb_setup_packet *)((u8 *)status_td + sizeof(uhci_td));

    *setup = packet;

    *setup_td = {};

    setup_td->link = data_td ? data_td_phys : status_td_phys;
    setup_td->token =
        USB_PID_SETUP |
        UHCI_TOKEN_ADDR(address) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA0) |
        UHCI_TOKEN_MAXLEN(8);

    setup_td->status =
        UHCI_TD_ACTIVE |
        UHCI_TD_CERR(3);

    setup_td->buffer = setup_phys;

    if (data_td) {
        // DATA TDs
        u32 offset = 0;
        u8 toggle = USB_DATA1;

        for (u32 i = 0; i < data_td_count; i++) {
            u32 remaining = length - offset;
            u32 length = remaining < maxpacketsize
                     ? remaining
                    : maxpacketsize;

            bool last = (i == data_td_count - 1);

            data_td[i].link =
                last
                    ? status_td_phys | UHCI_PTR_DEPTH
                 : data_td_phys +
                  (i + 1) * sizeof(uhci_td) |
                 UHCI_PTR_DEPTH;

            data_td[i].status =
                UHCI_TD_ACTIVE | UHCI_TD_CERR(3) | (last ? 0 : UHCI_TD_SPD);

            data_td[i].token =
                USB_PID_IN |
                UHCI_TOKEN_ADDR(address) |
                UHCI_TOKEN_EP(0) |
                UHCI_TOKEN_TOGGLE(toggle) |
                UHCI_TOKEN_MAXLEN(length);

            data_td[i].buffer = desc_phys + offset;

            offset += length;
            toggle ^= 1;
        }
    }

    *status_td = {};

    status_td->status =
        UHCI_TD_ACTIVE |
        UHCI_TD_CERR(3);

    bool data_in = setup->BmRequestType & 0x80;
    bool has_data = setup->wLength != 0;

    bool status_in = !has_data || !data_in;
    u8 status_pid = status_in ? USB_PID_IN : USB_PID_OUT;

    status_td->token =
        status_pid |
        UHCI_TOKEN_ADDR(address) |
        UHCI_TOKEN_EP(0) |
        UHCI_TOKEN_TOGGLE(USB_DATA1) |
        UHCI_TOKEN_MAXLEN(0);

    status_td->buffer = 0;

    status_td->link = UHCI_LINK_TERMINATE;

    *qh = {};

    qh->head_link = UHCI_LINK_TERMINATE;
    qh->element_link = setup_td_phys;


    u16 old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller temporarily

    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: controller failed to halt\n");
        free_dma(alloc.virtual_address);
        return nullptr;
    }

    u32 *backup = new u32[1024];

    for (int i = 0; i < 1024; i++) {
        backup[i] = frame_list[i];
        frame_list[i] = qh_phys | 0x2u;
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: controller failed to start\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        return nullptr;
    }

    u32 recieved = 0;
    auto result = wait_for_control_transfer(qh, setup_td, data_td, data_td_count,
                                            status_td, status_td_phys, timer, recieved);
    if (result != uhci_transfer_result::success) {
        printf("UHCI: send packet error\n");
        old_command = inw(io_base+UHCI_USBCMD);
        outw(io_base+UHCI_USBCMD, (old_command & ~1u)); // halt the controller

        if (!wait_for_halted(io_base, timer)) {
            printf("UHCI: controller failed to halt\n");
            free_dma(alloc.virtual_address);
            delete[] backup;
            return nullptr;
        }

        for (int i = 0; i < 1024; i++) 
            frame_list[i] = backup[i];
        
        asm volatile("mfence" ::: "memory");

        outw(io_base+UHCI_USBCMD, old_command | 1u); // start the controller

        if (!wait_for_start(io_base, timer)) {
            printf("UHCI: controller failed to start\n");
        }


        free_dma(alloc.virtual_address);
        delete[] backup;
        return nullptr;
    }

    wait_ms(timer, 2);

    old_command = inw(io_base+UHCI_USBCMD);
    outw(io_base+UHCI_USBCMD, (old_command & ~1u));
    if (!wait_for_halted(io_base, timer)) {
        printf("UHCI: failed to halt controller\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        return nullptr;
    }

    for (u32 i = 0; i < 1024; i++) {
        frame_list[i] = backup[i];
    }

    asm volatile("mfence" ::: "memory");
    outw(io_base+UHCI_USBCMD, (old_command | 1u)); // start the controller

    if (!wait_for_start(io_base, timer)) {
        printf("UHCI: failed to start controller\n");
        free_dma(alloc.virtual_address);
        delete[] backup;
        return nullptr;
    }

    delete[] backup;
    

    if (length) {
        void *ptr0 = (void *)((u8 *)setup + 8);
        void *ptr1 = kmalloc(length);
        if (ptr1 == nullptr) {
            free_dma(alloc.virtual_address);
            return nullptr;
        }
        memcpy(ptr1, ptr0, length);
        free_dma(alloc.virtual_address);
        return ptr1;
    } else { 
        free_dma(alloc.virtual_address);
        return (void *)1;
    }

}


int uhci_controller::configure_endpoint(usb_endpoint_descriptor endpoint, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer, u8 port) {
    usb_setup_packet packet = {
        0x00,
        0x09,
        bConfigurationValue,
        0,
        0
    };

    u64 code = (u64)usb_send_packet(packet, maxpacketsize, 0, address, timer, port);

    if (code != 1) {
        printf("UHCI: SET_CONFIGURATION for the device on port 0x%b failed\n", port);
        return 1; // return 1 for failure
    }


}