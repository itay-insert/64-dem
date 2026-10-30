#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/display/vga.h"

typedef struct {
    u8 cap_length;            // 0x00 CAPLENGTH
    u8 reserved;              // 0x01
    u16 hci_version;          // 0x02 HCIVERSION
    u32 hcs_params;           // 0x04 HCSPARAMS
    u32 hcc_params;           // 0x08 HCCPARAMS
    u8 port_route[8];         // 0x0C HCSP-PORTROUTE
} ehci_cap_regs_t;

// Operational registers begin at the BAR base plus CAPLENGTH.
typedef struct {
    u32 command;              // 0x00 USBCMD
    u32 status;               // 0x04 USBSTS
    u32 interrupt_enable;     // 0x08 USBINTR
    u32 frame_index;          // 0x0C FRINDEX
    u32 control_data_segment; // 0x10 CTRLDSSEGMENT
    u32 periodic_list_base;   // 0x14 PERIODICLISTBASE
    u32 async_list_address;   // 0x18 ASYNCLISTADDR
    u32 reserved[9];          // 0x1C-0x3F
    u32 config_flag;          // 0x40 CONFIGFLAG
    u32 port_status[15];      // 0x44 PORTSC[0..N_PORTS-1]
} ehci_op_regs_t;

static_assert(sizeof(ehci_cap_regs_t) == 0x14, "EHCI capability register layout");
static_assert(offsetof(ehci_op_regs_t, config_flag) == 0x40, "EHCI CONFIGFLAG offset");
static_assert(offsetof(ehci_op_regs_t, port_status) == 0x44, "EHCI PORTSC offset");



ehci_controller::ehci_controller() { // initialize the ehci_controller fields
    memcpy(vendor, "EHCI", sizeof(vendor));
    controller = {0};
    class_code = 0x0C;
    subclass = 0x03;
    prog_if = 0x20;
    type = MMIO;
    construct_controller();
}


#define USBSTS op_regs->status // the USB status register
#define USBCMD op_regs->command // the USB command register
#define USBINTR op_regs->interrupt_enable // the USB interrupt_enable register
#define CTRLDSSEGMENT op_regs->control_data_segment // the USB control_data_segment
#define PERIODICLISTBASE op_regs->periodic_list_base // the periodic list base address register
#define FRINDEX op_regs->frame_index // the frame_index register
#define ASYNCLISTADDR op_regs->async_list_address // async list address


#define HCHalted (1u << 12) // HCHalted bit 12 of USBSTS
#define HCRESET (1u << 1) // USBCMD.HCRESET bit 1 

#define EHCI_PORTSC 0x44

#define CCS (1 << 0) // current connect status
#define CSC (1 << 1) // connect change
#define PE (1 << 2) // port enable
#define PEC (1 << 3)  // port enable change
#define OCA (1 << 4) // over-current active
#define OCC (1 << 5) // over-current change
#define FPR (1 << 6) // force port resume
#define SUSP (1 << 7) // suspended
#define PR (1 << 8) // port reset
#define LINE_MASK (3 << 10) // line status
#define LINE_K (1 << 10)
#define POWER (1 << 12) // port power
#define OWNER (1 << 13) // companion controller owns port

#define W1C_MASK (CSC | PEC | OCC)

#define ASS (1u << 15)

int ehci_controller::controller_init(TimerSource &timer) {
    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base; // ehci_cap_regs is at controller.io_base
    volatile ehci_op_regs_t *op_regs = (volatile ehci_op_regs_t *)(controller.io_base + cap_regs->cap_length); // adding cap_length to get op_regs base

    u32 hcc = cap_regs->hcc_params;
    u8 eecp = (hcc >> 8) & 0xFF; // PCI config offset of first EHCI capability
    bool supports_64_bit = (hcc & 1) != 0; // check bit 0 of hcc for 64-bit support


    // ehci Ownership obtaining if needed
    while (eecp) {
        u32 header;
        if (pci_config_read(controller.pci_address, eecp, 4, &header) != PCI_STATUS_SUCCESS) 
            return 1;

        u8 capability_id = header & 0xFF; // read the capability_id from pci at offset eecp
        u8 next = (header >> 8) & 0xFF;

        if (capability_id == 1) {
            if (pci_write8(controller.pci_address, eecp+3, 1) != PCI_STATUS_SUCCESS) // take Ownership by writing 1 to pci at controller.pci_address, offset eecp+3
                return 1;

            int ms_target = 1000; // set 1 second timeout, 1000 ms = sec
            int ms_count = 0;
            while (pci_read8(controller.pci_address, eecp+2) & 1) { // PCI read to check whether Ownership was obtained
                if (ms_count == ms_target)
                    break;
                wait_ms(timer, 1);
                ms_count++;
            } 

            if (!(pci_read8(controller.pci_address, eecp+2) & 1)) {
                printf("EHCI: Ownership successfully obtained from bios\n");
                if (pci_write32(controller.pci_address, eecp+4, 0) != PCI_STATUS_SUCCESS) // pci write to disable USB SMI
                    return 1; // return failure if failed
                break;
            } else {
                printf("EHCI: Ownership TakeOver failed\n");
                return 1; // return failure if Ownership TakeOver failed
            }
        } else 
            eecp = next;
            
    }

    USBCMD &= ~1u; // clear bit 0 in the USBCMD register to send a stop/halt command

    unsigned ms = 0;
    while (!(USBSTS & HCHalted) && ms < 10) { // 10 ms timeout for the halt
        wait_ms(timer, 1);
        ms++;
    }

    if (!(USBSTS & HCHalted)) {  
        printf("EHCI: Controller failed to halt\n");
        return 1; // return failure if failed to halt
    }


    USBCMD |= HCRESET; // set HCRESET bit in USBCMD to request reset

    ms = 0;
    while ((USBCMD & HCRESET) && ms < 250) { // set 250 ms timeout for reset
        wait_ms(timer, 1);
        ms++;
    }


    if (USBCMD & HCRESET) {
        printf("EHCI: controller reset timed out!\n");
        return 1; // return failure if reset timed out
    }

    USBINTR = 0; // writing 0 to the USBINTR register to disable interrupts
    USBSTS = 0x3F; // writing 0x3F to the USBSTS register to clear pending event bits by a write-one-to-clear

    CTRLDSSEGMENT = 0; // set the control_data_segment to 0

    dma_ret PerFrameList = allocate_dma(4096); // allocate the periodic frame list
    if (PerFrameList.status != 0) {
        printf("EHCI: failed to allocate the periodic frame list\n");
        return 1; // abort if allocation failure
    }

    if (PerFrameList.physical_address > 0xFFFFFFFF) {
        printf("EHCI: periodic frame list physical address too big!\n");
        return 1; // abort if physical address too big
    }

    Frame_list = (u32 *)PerFrameList.virtual_address;
    for (int i = 0; i < 1024; i++) 
        Frame_list[i] = 1; // fill all the 1024 periodic frame list entries with TERMINATE (1)
    
    PERIODICLISTBASE = (u32)PerFrameList.physical_address;

    dma_ret AsyncHeadAlloc = allocate_dma(64);

    if (AsyncHeadAlloc.status != 0 || AsyncHeadAlloc.physical_address > 0xFFFFFFFF || (AsyncHeadAlloc.physical_address & 0x1F)) {
        printf("EHCI: failed to allocate async head QH\n");
        return 1;
    }

    async_head = (ehci_qh *)AsyncHeadAlloc.virtual_address;
    async_head_phys = (u32)AsyncHeadAlloc.physical_address;

    *async_head = {};  // zero-initialize the async head

    async_head->horizontal_link = async_head_phys | 0x02;

    async_head->endpoint_characteristics =
        (1u << 15) | // Head of reclamation list
        (2u << 12) | // High-speed dummy head
        (64u << 16); // maximum packet size

    async_head->endpoint_capabilities = 0;

    async_head->current_qtd = 0;
    async_head->next_qtd = 1;
    async_head->alternate_next_qtd = 1;
    async_head->token = 0;

    ASYNCLISTADDR = async_head_phys;
    // The periodic list contains only terminated entries. Keeping it enabled
    // advances FRINDEX without submitting any transfers.
    USBCMD = (USBCMD & ~((1u << 4) | (1u << 5))) |
             (1u << 4) | // Periodic schedule Enable
             (1u << 5) | // Asynchronous schedule Enable
             1u; 
    op_regs->config_flag = 1u;

    ms = 0;
    while ((USBSTS & HCHalted) && ms < 100) {  // wait for HCHalted to clear
        wait_ms(timer, 1);
        ms++;
    }

    if (USBSTS & HCHalted) {
        printf("EHCI: controller start timed out\n");
        return 1;
    }


    ms = 0;

    while (!(USBSTS & ASS) && ms < 100) { // wait for ASS to set
        wait_ms(timer, 1); 
        ms++;
    }

    if (!(USBSTS & ASS)) {
        printf("EHCI: asynchronous schedule failed to activate\n");
        return 1;
    }

    const u32 before = FRINDEX;
    for (u32 elapsed = 0; elapsed < 100 && FRINDEX == before; elapsed++) {
        wait_ms(timer, 1);
        if (USBSTS & HCHalted) {
            printf("EHCI: controller halted while waiting for frame_index\n");
            return 1;
        }
    }
    if (FRINDEX == before) {
        printf("EHCI: frame_index not advancing\n");
        return 1;
    }
    
    return 0;

}

static inline u32 ehci_read_port(u64 op_base, u8 port) {
    volatile u32 *Portsc = (volatile u32 *)(op_base + EHCI_PORTSC + (port << 2));
    return (u32)*Portsc;
}

int ehci_controller::enumrate_ports() {
    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;
    u64 cap_length = (u64)cap_regs->cap_length;
    u64 op_base = controller.io_base + cap_length; // calculate op_base from io_base + cap_length

    u8 ehci_port_count = (u8)(cap_regs->hcs_params & 0x0F);
    ports = new usb_port_descriptor[ehci_port_count];
    if (ports == nullptr)
        return -1; // return -1 for allocation failure

    for (u8 port = 0; port < ehci_port_count; port++) {
        u32 status = ehci_read_port(op_base, port);
        ports[port].port_number = port + 1;
        ports[port].port_status = status;
        ports[port].connected = (status & CCS);
        ports[port].controller_type = EHCI;
    }

    return (int)ehci_port_count;
}


int ehci_controller::reset_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;

    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;

    u64 op_base = controller.io_base + cap_regs->cap_length;

    volatile ehci_op_regs_t *op_regs = (volatile ehci_op_regs_t *)op_base;

    volatile u32 *portsc = (volatile u32 *)(op_base + EHCI_PORTSC + (u64)((port - 1) << 2));

    if (USBSTS & HCHalted)
        return CONTROLLER_HALTED;

    
    u32 status = *portsc;


    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (status & OWNER)
        return COMPANION_REQUIRED;

    if (!(status & CCS))
        return NO_DEVICE;

    if (status & FPR)
        return PORT_BUSY; // resume signaling is in progress

    // low-speed devices should be handed to the companion
    // controller before performing an EHCI port reset
    if (!(status & PE) && (status & LINE_MASK) == LINE_K) {
        u32 value = status & ~W1C_MASK;
        value &= ~PE;
        value |= OWNER;

        *portsc = value;
        (void)*portsc; // read back to flush MMIO register write
        
        return COMPANION_REQUIRED;
    }


    // assert reset and set PE to 0
    u32 value = status & ~W1C_MASK;
    value &= ~PE;
    value |= PR;

    *portsc = value;
    (void)*portsc; // read back to flush MMIO register write

    wait_ms(timer, 50); // wait 50 ms for USB 2.0

    status = *portsc;

    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (!(status & CCS))
        return NO_DEVICE;

    // deassert reset
    value = status & ~W1C_MASK;
    value &= ~(PE | PR);

    *portsc = value;
    (void)*portsc; // read back to flush MMIO register write


    // the controller must finish reset and stablize the port
    bool reset_complete = false;

    for (u32 elapsed = 0; elapsed < 10; elapsed++) {  // 10 ms timeout
        status = *portsc;

        if (status == 0xFFFFFFFF)
            return INVALID_STATUS;

        if (!(status & CCS))
            return NO_DEVICE;

        if (!(status & PR)) {
            reset_complete = true;
            break;
        } 

        wait_ms(timer, 1);
    }


    if (!reset_complete)
        return RESET_STILL_ASSERTED;

    wait_ms(timer, 10); // USB reset recovery wait

    status = *portsc;

    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (!(status & CCS)) 
        return NO_DEVICE;

    if (status & PR)
        return RESET_STILL_ASSERTED;

    // hardware enables a high-speed port automatically
    if (status & PE)
        return RESET_SUCCESS;

    // if connected but not enabled after reset, hand off to companion controller
    value = status & ~W1C_MASK;
    value &= ~PE;
    value |= OWNER;

    *portsc = value;
    (void)*portsc; // MMIO read to flush the write

    return COMPANION_REQUIRED;

}


u64 ehci_controller::port_to_address(u8 port) {
    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;

    u64 op_base = controller.io_base + (u64)cap_regs->cap_length;

    u64 port_base = op_base + EHCI_PORTSC + (u64)((port - 1) << 2);
    return port_base;
}

int ehci_controller::disable_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;
    
    volatile u32 *portsc = (volatile u32 *)port_to_address(port);
    
    u32 value = *portsc;

    if (value == 0xFFFFFFFF)
        return INVALID_STATUS;

    value &= ~W1C_MASK;
    value &= ~PE;
    *portsc = value; // write to disable the port
    (void)*portsc; // register read to flush MMIO write

    for (int i = 0; i < 100; i++) { // poll the status with 100 ms timeout
        if ((*portsc & PE) == 0)
            break;
        wait_ms(timer, 1);
    }

    return (*portsc & PE) ? RESET_FAILED : RESET_SUCCESS;
}


enum class ehci_transfer_result {
    success,
    timeout,
    transfer_error
};

ehci_transfer_result wait_for_descriptor(
    volatile ehci_qtd* setup_td,
    volatile ehci_qtd* data_td,
    volatile ehci_qtd* status_td,
    TimerSource& timer,
    u32& received_bytes)
{
    received_bytes = 0;

    constexpr u32 QTD_STATUS_ACTIVE  = 1u << 7;
    constexpr u32 QTD_STATUS_HALTED  = 1u << 6;
    constexpr u32 QTD_STATUS_BUFFER  = 1u << 5;
    constexpr u32 QTD_STATUS_BABBLE  = 1u << 4;
    constexpr u32 QTD_STATUS_XACTERR = 1u << 3;
    constexpr u32 QTD_STATUS_MISSED  = 1u << 2;

    constexpr u32 QTD_STATUS_ERROR_MASK =
        QTD_STATUS_HALTED  |
        QTD_STATUS_BUFFER  |
        QTD_STATUS_BABBLE  |
        QTD_STATUS_XACTERR |
        QTD_STATUS_MISSED;

    constexpr u32 REQUESTED_BYTES = 8;
    constexpr u32 TIMEOUT_MS = 500;

    for (u32 elapsed = 0; elapsed < TIMEOUT_MS; ++elapsed) {

        asm volatile("mfence" ::: "memory");

        u32 setup_token  = setup_td->token;
        u32 data_token   = data_td->token;
        u32 status_token = status_td->token;

        bool setup_active  = setup_token & QTD_STATUS_ACTIVE;
        bool data_active   = data_token & QTD_STATUS_ACTIVE;
        bool status_active = status_token & QTD_STATUS_ACTIVE;

        // Check errors on qTDs that have finished.
        if ((!setup_active && (setup_token & QTD_STATUS_ERROR_MASK)) ||
            (!data_active && (data_token & QTD_STATUS_ERROR_MASK)) ||
            (!status_active && (status_token & QTD_STATUS_ERROR_MASK))) {
            return ehci_transfer_result::transfer_error;
        }

        // Wait until all three stages finish.
        if (!setup_active && !data_active && !status_active) {

            u32 setup_remaining  = (setup_token >> 16) & 0x7FFF;
            u32 data_remaining   = (data_token >> 16) & 0x7FFF;
            u32 status_remaining = (status_token >> 16) & 0x7FFF;

            // SETUP must transfer all 8 bytes.
            if (setup_remaining != 0)
                return ehci_transfer_result::transfer_error;

            // STATUS is a zero-length transfer.
            if (status_remaining != 0)
                return ehci_transfer_result::transfer_error;

            if (data_remaining > REQUESTED_BYTES)
                return ehci_transfer_result::transfer_error;

            received_bytes = REQUESTED_BYTES - data_remaining;

            return ehci_transfer_result::success;
        }

        wait_ms(timer, 1);
    }

    return ehci_transfer_result::timeout;
}


void insert_async_qh(ehci_qh *head, u32 head_phys, ehci_qh *qh, u32 qh_phys) {
    // Preserve the previous successor.
    u32 old_next = head->horizontal_link;

    // link qh to the head
    qh->horizontal_link = old_next;

    // ensure Qh contents are visible before publishing the link
    asm volatile("mfence" ::: "memory");

    // make the qh the head's successor
    head->horizontal_link = qh_phys | 0x02;

}


usb_device_descriptor ehci_controller:identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};

    dma_ret alloc = allocate_dma(320);

    if (alloc.status != 0 || alloc.physical_address > 0xFFFFFFFF) {
        printf("EHCI: failed to allocate the setup packet\n");
        return device_descriptor;
    }
    
    u64 setup_off = sizeof(ehci_qh) + (sizeof(ehci_qtd) * 3);
    u64 desc_off = setup_off + 8;

    u64 setup_phys = (alloc.physical_address + setup_off);
    u64 descriptor_phys = (alloc.physical_address + desc_off);

    u32 qh_phys = (u32)alloc.physical_address;

    u64 base = alloc.virtual_address;

    ehci_qh *qh = (ehci_qh *)base;

    base += sizeof(ehci_qh);

    ehci_qtd *setup_qtd = (ehci_qtd *)base;

    base += sizeof(ehci_qtd);

    ehci_qtd *data_qtd = (ehci_qtd *)base;

    base += sizeof(ehci_qtd);

    ehci_qtd *status_qtd = (ehci_qtd *)base;

    base += sizeof(ehci_qtd);

    usb_setup_packet *setup = (usb_setup_packet *)base;

    *setup = {
        0x80, // device to host
        0x06, // GET_DESCRIPTOR
        0x0100, // device descriptor
        0x0000, // index
        8  // request 8 bytes
    };

    *qh = {};   // zero-intialize

    qh->horizontal_link = 1; // terminate_link

    qh->endpoint_characteristics =
    (0u << 0)  |  // Device address 0
    (0u << 8)  |  // Endpoint number 0
    (2u << 12) | // High-speed
    (1u << 14) |  // Head of reclamation list (not required for this transfer)
    (1u << 16);   // Maximum packet size: 64 bytes

    qh->endpoint_capabilities = 0;

    qh->current_qtd = 0;
    qh->alternate_next_qtd = 1;
    qh->token = 0;

    qh->next_qtd = (u32)(alloc.physical_address + 64); // setup_td is at offset 64

    *setup_qtd = {};   // zero-intialize



    setup_qtd->next_qtd = (u32)(alloc.physical_address + 128); // data_td is at offset 128
    setup_qtd->alternate_next_qtd = 1;

    setup_qtd->token = 
        (2u << 8) |  // SETUP PID
        (3u << 10) |  // Error Counter
        (8u << 16) | // 8 bytes
        (1u << 7);  // active

    setup_qtd->buffer[0] = setup_phys;


    *data_qtd = {};   // zero-intialize

    u32 status_qtd_phys = (u32)(alloc.physical_address + 192); // status_qtd is at offset 192
    data_qtd->next_qtd = status_qtd_phys;
    data_qtd->alternate_next_qtd = status_qtd_phys;

    data_qtd->token = 
        (1u << 31) | // DATA1 toggle
        (1u << 8) | // IN PID
        (3u << 10) | // Error Counter
        (8u << 16) | // 8 bytes
        (1u << 7); // active

    data_qtd->buffer[0] = descriptor_phys;


    *status_qtd = {}; // zero-initialize


    status_qtd->next_qtd = 1u; // 1u == ehci_terminate_link
    status_qtd->alternate_next_qtd = 1u;

    status_qtd->token =
        (1u << 31) | // DATA1 toggle
        (0u << 8) | // OUT PID
        (3u << 10) | // Error counter
        (1u << 7); // active

    
    insert_async_qh(async_head, async_head_phys, qh, qh_phys);

    u32 recieved = 0;

    auto result = wait_for_descriptor(setup_qtd, data_qtd, status_qtd, timer, recieved);
    

}