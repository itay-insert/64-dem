#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/periodic.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/kmalloc.h"


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
    controller_type = EHCI;
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

#define EHCI_USBCMD_IAAD  (1u << 6)
#define EHCI_USBSTS_IAA   (1u << 5)

#define EHCI_QTD_ACTIVE   (1u << 7)

int ehci_controller::controller_init(TimerSource &timer) {
    states = nullptr;
    state_count = 0;

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


u32 REQUESTED_BYTES = 8;

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


    constexpr u32 TIMEOUT_MS = 500;

    for (u32 elapsed = 0; elapsed < TIMEOUT_MS; ++elapsed) {

        asm volatile("mfence" ::: "memory");

        u32 setup_token  = setup_td->token;
        u32 data_token   = 0;
        u32 status_token = status_td->token;

        if (data_td)
            data_token = data_td->token;

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
            if (data_td) {
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
            }

            return ehci_transfer_result::success;
        }

        wait_ms(timer, 1);
    }

    return ehci_transfer_result::timeout;
}


struct ehci_async_link_state {
    ehci_qh* previous;
    ehci_qh* inserted;
    u32 old_next;
    bool linked;
};

void insert_async_qh(ehci_qh *head, u32 head_phys, ehci_qh *qh, u32 qh_phys, ehci_async_link_state& state) {
    state.previous = head;
    state.inserted = qh;
    state.old_next = head->horizontal_link;
    state.linked = true;

    // Preserve the previous successor.
    u32 old_next = head->horizontal_link;

    // link qh to the head
    qh->horizontal_link = old_next;

    // ensure Qh contents are visible before publishing the link
    asm volatile("mfence" ::: "memory");

    // make the qh the head's successor
    head->horizontal_link = qh_phys | 0x02;

}


bool unlink_async_qh(volatile ehci_op_regs_t* regs, ehci_async_link_state& state, ehci_qh* qh,
     ehci_qtd* setup_qtd, ehci_qtd* data_qtd, ehci_qtd* status_qtd, TimerSource &timer) {
        if (!state.linked)
            return true;

        u32 data_token = 0;
        if (data_qtd)
            data_token = data_qtd->token;
        if ((qh->token & EHCI_QTD_ACTIVE) || (setup_qtd->token & EHCI_QTD_ACTIVE) ||
        (data_token & EHCI_QTD_ACTIVE) || (status_qtd->token & EHCI_QTD_ACTIVE)) {
            printf("EHCI: qh or qtd still active\n");
            return false;
        }

        if (regs->command & EHCI_USBCMD_IAAD)
            return false;

        regs->status = EHCI_USBSTS_IAA;

        state.previous->horizontal_link = state.old_next;
        

        qh->horizontal_link = state.old_next;

        asm volatile("mfence" ::: "memory");

        u32 command = regs->command;
        regs->command = command | EHCI_USBCMD_IAAD;

        volatile u32 readback = regs->command;
        (void)readback;


        for (u32 elapsed = 0; elapsed < 100; elapsed++) {
            if (regs->status & EHCI_USBSTS_IAA) {

                regs->status = EHCI_USBSTS_IAA;

                asm volatile("mfence" ::: "memory");

                state.linked = false;
                return true;
            }

            wait_ms(timer, 1);
        }

        printf("EHCI: timeout waiting for asynchronous advance\n");
        return false;
}



usb_device_descriptor ehci_controller::identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};
    REQUESTED_BYTES = 8;

    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;
    volatile ehci_op_regs_t *op_regs = (volatile ehci_op_regs_t *)(controller.io_base + (u64)cap_regs->cap_length);

    dma_ret alloc = allocate_dma(320);

    if (alloc.status != 0 || alloc.physical_address > 0xFFFFFFFF) {
        printf("EHCI: failed to allocate the setup packet\n");
        return device_descriptor;
    }
    const u32 setup_qtd_phys =
          (u32)(alloc.physical_address + sizeof(ehci_qh));
    const u32 data_qtd_phys =
          setup_qtd_phys + sizeof(ehci_qtd);
    const u32 status_qtd_phys =
          data_qtd_phys + sizeof(ehci_qtd);
    
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
    (64u << 16);  // Maximum packet size: 64 bytes

    qh->endpoint_capabilities = 0;

    qh->current_qtd = 0;
    qh->alternate_next_qtd = 1;
    qh->token = 0;

    qh->next_qtd = setup_qtd_phys;

    *setup_qtd = {};   // zero-intialize



    setup_qtd->next_qtd = data_qtd_phys;
    setup_qtd->alternate_next_qtd = 1;

    setup_qtd->token = 
        (2u << 8) |  // SETUP PID
        (3u << 10) |  // Error Counter
        (8u << 16) | // 8 bytes
        (1u << 7);  // active

    setup_qtd->buffer[0] = setup_phys;


    *data_qtd = {};   // zero-intialize

    
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

    
    ehci_async_link_state link_state = {};


    insert_async_qh(async_head, async_head_phys, qh, qh_phys, link_state);

    u32 recieved = 0;

    auto result = wait_for_descriptor(setup_qtd, data_qtd, status_qtd, timer, recieved);
    
    if (!unlink_async_qh(op_regs, link_state, qh, setup_qtd, data_qtd, status_qtd, timer)) {
        printf("EHCI: unlink async_qh error in identify_device\n");
        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;
        *new_cache.error_code = (EHCI << 8) | LINK_ISSUE;
        *new_cache.dma = alloc;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;
        
        controller.cached.cache = cache;
        controller.cached.max++;

        return device_descriptor;
    }

    if (result != ehci_transfer_result::success) {
        printf("EHCI: descriptor transfer error\n");
        free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    usb_device_descriptor *desc = (usb_device_descriptor *)(alloc.virtual_address + desc_off);

    if (recieved == 8 && desc->bLength == 18 && desc->bDescriptorType == 1) {
        usb_device_descriptor *device_ptr = &device_descriptor;
        memcpy(device_ptr, desc, 8);
    }

    free_dma(alloc.virtual_address);
    return device_descriptor;
}


// Link pointers
#define EHCI_LINK_TERMINATE  (1u << 0)
#define EHCI_LINK_TYPE_QH    (1u << 1)

// qTD token
#define EHCI_QTD_TOGGLE      (1u << 31)
#define EHCI_QTD_BYTES(n)    ((u32)(n) << 16)
#define EHCI_QTD_IOC         (1u << 15)
#define EHCI_QTD_CERR(n)     ((u32)(n) << 10)

#define EHCI_QTD_PID_OUT     (0u << 8)
#define EHCI_QTD_PID_IN      (1u << 8)
#define EHCI_QTD_PID_SETUP   (2u << 8)

#define EHCI_QTD_ACTIVE      (1u << 7)

// QH endpoint characteristics
#define EHCI_QH_CONTROL      (1u << 27)
#define EHCI_QH_MAX_PACKET(n) ((u32)(n) << 16)
#define EHCI_QH_HEAD         (1u << 15)
#define EHCI_QH_DTC          (1u << 14)

#define EHCI_QH_FULL_SPEED   (0u << 12)
#define EHCI_QH_LOW_SPEED    (1u << 12)
#define EHCI_QH_HIGH_SPEED   (2u << 12)

#define EHCI_QH_ENDPOINT(n)  ((u32)(n) << 8)
#define EHCI_QH_ADDRESS(n)   ((u32)(n))
#define EHCI_QH_NAK_RELOAD(n) ((u32)(n) << 28)

// Asynchronous schedule
#define EHCI_USBCMD_ASE      (1u << 5)
#define EHCI_USBCMD_IAAD     (1u << 6)
#define EHCI_USBSTS_ASS      (1u << 15)
#define EHCI_USBSTS_IAA      (1u << 5)


static u8 next_usb_address = 1;

static inline u8 allocate_usb_address(void) {
    if (next_usb_address > 127) 
        return 0;  // no addresses left

    return next_usb_address++;
}


usb_device ehci_controller::set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port) {
    usb_device device = {0};
    if (desc.bLength != 18 || desc.bDescriptorType != 1 ||
        desc.bMaxPacketSize0 != 64)
        return device;
    REQUESTED_BYTES = 18;

    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;
    volatile ehci_op_regs_t *op_regs = (volatile ehci_op_regs_t *)(controller.io_base + (u64)cap_regs->cap_length);


    u64 size = sizeof(ehci_qh) + (sizeof(ehci_qtd) << 1) + 8;
    dma_ret alloc = allocate_dma(size); // allocate the dma for set_address

    if (alloc.status != 0) {
        printf("EHCI: SET ADDRESS allocation failed\n");
        device.discovered = false;
        return device;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("EHCI: SET ADDRESS physical address too big\n");
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    u32 qh_phys = (u32)alloc.physical_address;
    u32 setup_qtd_phys = qh_phys + sizeof(ehci_qh);
    u32 status_qtd_phys = setup_qtd_phys + sizeof(ehci_qtd);
    u32 setup_phys = status_qtd_phys + sizeof(ehci_qtd);

    ehci_qh *qh = (ehci_qh *)alloc.virtual_address;
    ehci_qtd *setup_qtd = (ehci_qtd *)(alloc.virtual_address + sizeof(ehci_qh));
    ehci_qtd *status_qtd = (ehci_qtd *)((u8 *)setup_qtd + sizeof(ehci_qtd));
    usb_setup_packet *setup = (usb_setup_packet *)((u8 *)status_qtd + sizeof(ehci_qtd));


    u8 address = allocate_usb_address();
    if (!address) {
        free_dma(alloc.virtual_address);
        return device;
    }

    // setup packet init
    *setup = {
        .BmRequestType = 0x00,
        .bRequest = 0x05,
        .wValue = address,
        .wIndex = 0,
        .wLength = 0
    };

    *qh = {};

    qh->endpoint_characteristics =
        EHCI_QH_NAK_RELOAD(4) |
        EHCI_QH_ADDRESS(0) |
        EHCI_QH_ENDPOINT(0) |
        EHCI_QH_HIGH_SPEED |
        EHCI_QH_DTC |
        EHCI_QH_MAX_PACKET(64);


    qh->endpoint_capabilities = 0;

    qh->current_qtd = 0;
    
    qh->next_qtd = setup_qtd_phys;
    qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
    qh->token = 0;

    for (int i = 0; i < 5; i++) {
        qh->buffer[i] = 0;
        qh->extended_buffer[i] = 0;
    }

    // setup_qtd init
    *setup_qtd = {};

    setup_qtd->token =
        EHCI_QTD_ACTIVE |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_SETUP |
        EHCI_QTD_BYTES(8);

    setup_qtd->next_qtd = status_qtd_phys;
    setup_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    setup_qtd->buffer[0] = setup_phys;

    for (int i = 1; i < 5; i++) {
        setup_qtd->buffer[i] = 0;
        setup_qtd->extended_buffer[i] = 0;
    }

    setup_qtd->extended_buffer[0] = 0;


    *status_qtd = {};

    status_qtd->token =
        EHCI_QTD_ACTIVE |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_IN |
        EHCI_QTD_TOGGLE |
        EHCI_QTD_BYTES(0) |
        EHCI_QTD_IOC;

    for (int i = 0; i < 5; i++) {
        status_qtd->buffer[i] = 0;
        status_qtd->extended_buffer[i] = 0;
    }

    status_qtd->next_qtd = EHCI_LINK_TERMINATE;
    status_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    ehci_async_link_state link_state = {};

    insert_async_qh(async_head, async_head_phys, qh, qh_phys, link_state); // insert the qh to async_head

    u32 recieved = 0;

    auto result = wait_for_descriptor(setup_qtd, nullptr, status_qtd, timer, recieved);

    if (!unlink_async_qh(op_regs, link_state, qh, setup_qtd, nullptr, status_qtd, timer)) {
        printf("EHCI: unlink async_qh error in set_address\n");
        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;
        *new_cache.error_code = (EHCI << 8) | LINK_ISSUE;
        *new_cache.dma = alloc;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;
        
        controller.cached.cache = cache;
        controller.cached.max++;

        device.discovered = false;
        return device;
    }

    if (result != ehci_transfer_result::success) {
        printf("EHCI: SET ADDRESS for port 0x%b failed\n", port);
        
        next_usb_address = address;
        device.discovered = false;
        free_dma(alloc.virtual_address);
        return device;
    }

    device.address = address; // save the usb address
    wait_ms(timer, 2);

    free_dma(alloc.virtual_address);

    size = sizeof(ehci_qh) + (sizeof(ehci_qtd) * 3) + 8 + 18;
    alloc = allocate_dma(size); // allocate GET DESCRIPTOR

    if (alloc.status != 0) {
        printf("EHCI: GET DESCRIPTOR allocation failed\n");
        device.discovered = false;
        return device;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("EHCI: GET DESCRIPTOR physical address too big\n");
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    // calculate sizes
    qh_phys = (u32)alloc.physical_address;
    setup_qtd_phys = qh_phys + sizeof(ehci_qh);
    u32 data_qtd_phys = setup_qtd_phys + sizeof(ehci_qtd);
    status_qtd_phys = data_qtd_phys + sizeof(ehci_qtd);
    setup_phys = status_qtd_phys + sizeof(ehci_qtd);
    u32 desc_phys = setup_phys + 8;

    qh = (ehci_qh *)alloc.virtual_address;
    setup_qtd = (ehci_qtd *)(alloc.virtual_address + sizeof(ehci_qh));
    ehci_qtd *data_qtd = (ehci_qtd *)((u8 *)setup_qtd + sizeof(ehci_qtd));
    status_qtd = (ehci_qtd *)((u8 *)data_qtd + sizeof(ehci_qtd));
    setup = (usb_setup_packet *)((u8 *)status_qtd + sizeof(ehci_qtd));

    *setup = {
        .BmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue = 0x0100,
        .wIndex = 0,
        .wLength = 18
    };


    // qh init
    *qh = {};

    qh->endpoint_characteristics =
        EHCI_QH_NAK_RELOAD(4) |
        EHCI_QH_ADDRESS(address) |
        EHCI_QH_ENDPOINT(0) |
        EHCI_QH_HIGH_SPEED |
        EHCI_QH_DTC |
        EHCI_QH_MAX_PACKET(desc.bMaxPacketSize0);

    qh->endpoint_capabilities = 0;
    qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
    qh->next_qtd = setup_qtd_phys;

    qh->current_qtd = 0;

    qh->token = 0;

    *setup_qtd = {};

    setup_qtd->token =
        EHCI_QTD_ACTIVE |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_SETUP |
        EHCI_QTD_BYTES(8); // DATA0

    setup_qtd->next_qtd = data_qtd_phys;
    setup_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    setup_qtd->buffer[0] = setup_phys;


    *data_qtd = {};

    data_qtd->token =
        EHCI_QTD_ACTIVE |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_IN |
        EHCI_QTD_TOGGLE |
        EHCI_QTD_BYTES(18);

    data_qtd->next_qtd = status_qtd_phys;
    data_qtd->alternate_next_qtd = status_qtd_phys;

    data_qtd->buffer[0] = desc_phys;

    *status_qtd = {};

    status_qtd->token =
        EHCI_QTD_ACTIVE |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_OUT |
        EHCI_QTD_TOGGLE |
        EHCI_QTD_BYTES(0) |
        EHCI_QTD_IOC;

    status_qtd->next_qtd = EHCI_LINK_TERMINATE;
    status_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    link_state = {};

    insert_async_qh(async_head, async_head_phys, qh, qh_phys, link_state);

    recieved = 0;

    result = wait_for_descriptor(setup_qtd, data_qtd, status_qtd, timer, recieved);

    if (!unlink_async_qh(op_regs, link_state, qh, setup_qtd, data_qtd, status_qtd, timer)) {
        printf("EHCI: unlink async_qh error in GET DESCRIPTOR\n");
        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;
        *new_cache.error_code = (EHCI << 8) | LINK_ISSUE;
        *new_cache.dma = alloc;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;
        
        controller.cached.cache = cache;
        controller.cached.max++;

        device.discovered = false;
        return device;
    }

    if (result != ehci_transfer_result::success) {
        printf("EHCI: GET DESCRIPTOR for port 0x%b failed\n", port);

        int rests = reset_port(port, timer);
        if (rests != 0) {
            printf("EHCI: failed to reset port 0x%b\n", port);
        } else {
            next_usb_address = address;
        }

        free_dma(alloc.virtual_address);
        device.discovered = false;
        device.address = 0;
        return device;
    }

    usb_device_descriptor *desc_ptr = (usb_device_descriptor *)((u8 *)setup + 8);
    bool valid_descriptor = recieved == 18 &&
        desc_ptr->bDescriptorType == 0x01 && desc_ptr->bLength == 18;
    if (valid_descriptor) {
        memcpy(&desc, desc_ptr, 18);
    }

    device.descriptor = desc;
    device.discovered = valid_descriptor;
    device.speed = USB_SPEED_HIGH;
    free_dma(alloc.virtual_address);
    return device;
}


#define EHCI_QH_TYPE_QH       (1u << 1)

#define EHCI_QH_RL(v)         ((u32)(v) << 28)
#define EHCI_QH_CONTROL_EP    (1u << 27)
#define EHCI_QH_MPS(v)        (((u32)(v) & 0x7FF) << 16)
#define EHCI_QH_HEAD          (1u << 15)
#define EHCI_QH_DTC           (1u << 14)
#define EHCI_QH_SPEED(v)      (((u32)(v) & 3) << 12)
#define EHCI_QH_EP(v)         (((u32)(v) & 0xF) << 8)
#define EHCI_QH_ADDR(v)       ((u32)(v) & 0x7F)

#define EHCI_SPEED_FULL       0
#define EHCI_SPEED_LOW        1
#define EHCI_SPEED_HIGH       2

#define EHCI_QH_MULT(v)       (((u32)(v) & 3) << 30)
#define EHCI_QH_HUB_PORT(v)   (((u32)(v) & 0x7F) << 23)
#define EHCI_QH_HUB_ADDR(v)   (((u32)(v) & 0x7F) << 16)
#define EHCI_QH_C_MASK(v)     (((u32)(v) & 0xFF) << 8)
#define EHCI_QH_S_MASK(v)     ((u32)(v) & 0xFF)

void *ehci_controller::usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port) {
    if (usb_transfer_pending() || controller.needs_reinit) return nullptr; // EP0 must not steal pending DMA/events.

    if (!maxpacketsize || length != packet.wLength || length > 4096 ||
        (length && !(packet.BmRequestType & IN)))
        return nullptr;
    REQUESTED_BYTES = length;
    volatile ehci_cap_regs_t *cap_regs = (volatile ehci_cap_regs_t *)controller.io_base;
    volatile ehci_op_regs_t *op_regs = (volatile ehci_op_regs_t *)(controller.io_base + (u64)cap_regs->cap_length);


    
    u64 size = sizeof(ehci_qh) + sizeof(ehci_qtd) * (length > 0 ? 3 : 2) + length + 8;

    dma_ret alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("EHCI: packet allocation failed\n");
        return nullptr;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("EHCI: packet physical address too big!\n");
        free_dma(alloc.virtual_address);
        return nullptr;
    }    

    u32 qh_phys = (u32)alloc.physical_address;
    u32 setup_td_phys = qh_phys + sizeof(ehci_qh);
    u32 data_td_phys = 0;
    u32 status_td_phys = 0;
    if (length > 0) {
        data_td_phys = setup_td_phys + sizeof(ehci_qtd);
        status_td_phys = data_td_phys + sizeof(ehci_qtd);
    } else {
        status_td_phys = setup_td_phys + sizeof(ehci_qtd);
    }

    u32 setup_phys = status_td_phys + sizeof(ehci_qtd);
    u32 data_phys = setup_phys + sizeof(usb_setup_packet);

    ehci_qh *qh = (ehci_qh *)alloc.virtual_address;
    ehci_qtd *setup_qtd = (ehci_qtd *)(alloc.virtual_address + sizeof(ehci_qh));
    ehci_qtd *data_qtd = nullptr;
    ehci_qtd *status_qtd = nullptr;
    if (length > 0) {
        data_qtd = (ehci_qtd *)((u8 *)setup_qtd + sizeof(ehci_qtd));
        status_qtd = (ehci_qtd *)((u8 *)data_qtd + sizeof(ehci_qtd));
    } else {
        status_qtd = (ehci_qtd *)((u8 *)setup_qtd + sizeof(ehci_qtd));
    }

    usb_setup_packet *setup = (usb_setup_packet *)((u8 *)status_qtd + sizeof(ehci_qtd));

    *setup = packet;

    *setup_qtd = {};

    setup_qtd->next_qtd = length ? data_td_phys : status_td_phys;

    setup_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    setup_qtd->token =
        EHCI_QTD_BYTES(8) |
        EHCI_QTD_CERR(3) |
        EHCI_QTD_PID_SETUP |
        EHCI_QTD_ACTIVE;

    setup_qtd->buffer[0] = setup_phys;

    bool data_in = setup->BmRequestType & 0x80;

    if (data_qtd) {

        *data_qtd = {};

        data_qtd->next_qtd = status_td_phys;

        data_qtd->alternate_next_qtd =
            data_in
                ? status_td_phys
                : EHCI_LINK_TERMINATE;

        data_qtd->token =
            EHCI_QTD_TOGGLE |
            EHCI_QTD_BYTES(length) |
            EHCI_QTD_CERR(3) |
            (data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT) |
            EHCI_QTD_ACTIVE;

        data_qtd->buffer[0] = data_phys;
        const u32 bytes_to_page_end = 4096u - (data_phys & 4095u);
        if (length > bytes_to_page_end)
            data_qtd->buffer[1] = (data_phys + 4095u) & ~4095u;
    
    }

    bool status_in =
        setup->wLength == 0 || !data_in;

    u32 status_pid = status_in
        ? EHCI_QTD_PID_IN
        : EHCI_QTD_PID_OUT;

    *status_qtd = {};

    status_qtd->next_qtd = EHCI_LINK_TERMINATE;
    status_qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;

    status_qtd->token =
        EHCI_QTD_TOGGLE |
        EHCI_QTD_BYTES(0) |
        EHCI_QTD_CERR(3) |
        status_pid |
        EHCI_QTD_ACTIVE;

    *qh = {};

    qh->endpoint_characteristics =
        EHCI_QH_RL(4) |
        EHCI_QH_ADDR(address) |
        EHCI_QH_EP(0) |
        EHCI_QH_SPEED(EHCI_SPEED_HIGH) |
        EHCI_QH_DTC |
        EHCI_QH_MPS(maxpacketsize);
    qh->endpoint_capabilities = 0;
    qh->current_qtd = 0;
    qh->next_qtd = setup_td_phys;
    qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
    qh->token = 0;

    ehci_async_link_state link_state = {};

    insert_async_qh(async_head, async_head_phys, qh, qh_phys, link_state); // insert the qh to async_head

    u32 recieved = 0;

    auto result = wait_for_descriptor(setup_qtd, data_qtd, status_qtd, timer, recieved);

    if (!unlink_async_qh(op_regs, link_state, qh, setup_qtd, data_qtd, status_qtd, timer)) {
        printf("EHCI: unlink async_qh error in send packet\n");
        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;
        *new_cache.error_code = (EHCI << 8) | LINK_ISSUE;
        *new_cache.dma = alloc;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;
        
        controller.cached.cache = cache;
        controller.cached.max++;

        return nullptr;
    }

    if (result != ehci_transfer_result::success) {
        printf("EHCI: Send packet for port 0x%b failed\n", port);
        
        free_dma(alloc.virtual_address);
        return nullptr;
    }

    wait_ms(timer, 2);

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



static inline ehci_address_state *allocate_state(ehci_address_state *states, u8 state_count, usb_device device) {
    if (state_count == 0) {
        states = new ehci_address_state[1];
        states->interfaces = nullptr;
        states->device_address = device.address;
        states->interfaces_count = device.usb_interface_count;
        return states;
    } // if state_count is 0, allocate states

    u64 old_size = state_count * sizeof(ehci_address_state);
    u64 new_size = old_size + sizeof(ehci_address_state);
    states = (ehci_address_state *)krealloc(states, old_size, new_size); // else increase the former allocation
    states[state_count].interfaces = nullptr;
    states[state_count].interfaces_count = device.usb_interface_count;
    states[state_count].device_address = device.address;
    return states;
}

ehci_endpoint_state ehci_controller::configure_endpoint(usb_device device, u16 endpoint_index, u16 interface_index, TimerSource &timer) {
    ehci_endpoint_state endpoint = {0};

    u64 size = sizeof(ehci_qh) + (sizeof(ehci_qtd) << 1) + device.interfaces[interface_index].endpoints[endpoint_index].max_packet_size;
    dma_ret alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("EHCI: configure_endpoint allocation error\n");
        endpoint.dma = alloc;
        return endpoint;
    }

    if (alloc.physical_address >= 0x100000000ULL ||
        size > 0x100000000ULL - alloc.physical_address) {
        printf("EHCI: configure_endpoint allocation physical address too big\n");
        free_dma(alloc.virtual_address);
        alloc.status = 1;
        endpoint.dma = alloc;
        return endpoint;
    }

    u16 raw = device.interfaces[interface_index].endpoints[endpoint_index].descriptor.wMaxPacketSize;
    endpoint.dma = alloc;
    endpoint.max_packet_size = device.interfaces[interface_index].endpoints[endpoint_index].max_packet_size;
    endpoint.interval = device.interfaces[interface_index].endpoints[endpoint_index].interval;
    endpoint.device_address = device.address;
    endpoint.endpoint_address = device.interfaces[interface_index].endpoints[endpoint_index].address;
    endpoint.transfer_type = device.interfaces[interface_index].endpoints[endpoint_index].transfer_type;
    endpoint.speed = device.speed;

    u32 qh_phys = (u32)alloc.physical_address;
    u32 dummy_qtd_phys = qh_phys + sizeof(ehci_qh);
    endpoint.data_qtd_phys = dummy_qtd_phys + sizeof(ehci_qtd);
    endpoint.buffer_phys = dummy_qtd_phys + (sizeof(ehci_qtd) << 1);

    ehci_qh *qh = (ehci_qh *)alloc.virtual_address;
    ehci_qtd *dummy_qtd = (ehci_qtd *)(alloc.virtual_address + sizeof(ehci_qh));
    endpoint.data_qtd = (ehci_qtd *)((u8 *)dummy_qtd + sizeof(ehci_qtd));
    endpoint.buffer = (void *)((u8 *)dummy_qtd + (sizeof(ehci_qtd) << 1));

    endpoint.qh_phys = qh_phys;
    endpoint.dummy_qtd_phys = dummy_qtd_phys;
    endpoint.qh = qh;
    endpoint.dummy_qtd = dummy_qtd;

    *qh = {};
    *dummy_qtd = {};
    *endpoint.data_qtd = {};
    qh->horizontal_link = 1;
    qh->next_qtd = 1;
    qh->alternate_next_qtd = 1;
    dummy_qtd->next_qtd = 1;
    dummy_qtd->alternate_next_qtd = 1;
    endpoint.data_qtd->next_qtd = 1;
    endpoint.data_qtd->alternate_next_qtd = 1;

    endpoint.scheduled = false;
    endpoint.mult = (raw >> 11) & 0x03;

    *qh = {};
    *dummy_qtd = {};
    *endpoint.data_qtd = {};

    qh->horizontal_link = 1;
    qh->next_qtd = 1;
    qh->alternate_next_qtd = 1;

    qh->endpoint_characteristics =
        ((u32)endpoint.device_address & 0x7fu) |
        ((u32)(endpoint.endpoint_address & 0x0fu) << 8) |
        (2u << 12) | // EHCI encoding: high speed.
        ((u32)endpoint.max_packet_size << 16);
    // DTC=0: QH overlay maintains the data toggle.
    // H=0: only async_head is the reclamation head.

    qh->endpoint_capabilities = 1u << 30; // Mult=1.


    dummy_qtd->next_qtd = 1;
    dummy_qtd->alternate_next_qtd = 1;


    endpoint.data_qtd->next_qtd = 1;
    endpoint.data_qtd->alternate_next_qtd = 1;


    return endpoint;
}


int ehci_controller::configure_endpoints(usb_device device, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer) {
    states = allocate_state(states, state_count, device);
    state_count++;


    u16 interfaces_count = states[state_count-1].interfaces_count;
    // we wanna use state_count-1 because state_count represents the highest object
    states[state_count-1].interfaces = new ehci_interface_state[interfaces_count];

    for (u16 i = 0; i < interfaces_count; i++) {
        states[state_count-1].interfaces[i].interface_number = device.interfaces[i].descriptor.bInterfaceNumber;
        states[state_count-1].interfaces[i].endpoint_count = device.interfaces[i].endpoint_count;
        states[state_count-1].interfaces[i].endpoints = new ehci_endpoint_state[device.interfaces[i].endpoint_count];
        for (u16 j = 0; j < states[state_count-1].interfaces[i].endpoint_count; j++) {
            states[state_count-1].interfaces[i].endpoints[j] = configure_endpoint(device, j, i, timer);
            if (states[state_count-1].interfaces[i].endpoints[j].dma.status != 0)
                return 1;
        }
    }

    return 0;
}

#define ASS (1u << 15)
#define ASE (1u << 5)

static constexpr u32 EHCI_PSE = 1u << 4;
static constexpr u32 EHCI_PSS = 1u << 14;

static bool wait_periodic_state(volatile ehci_op_regs_t *op, bool enabled,
                                TimerSource &timer) {
    for (u32 elapsed = 0; elapsed < 100; ++elapsed) {
        if (((op->status & EHCI_PSS) != 0) == enabled) return true;
        wait_ms(timer, 1);
    }
    return ((op->status & EHCI_PSS) != 0) == enabled;
}

bool ehci_controller::link_interrupt_qh(ehci_endpoint_state &ep, TimerSource &timer) {
    const auto plan = usb_periodic::high_speed_plan(ep.interval);
    if (!Frame_list || !plan.frames || ep.speed != USB_SPEED_HIGH || ep.mult)
        return false;
    auto *cap = (volatile ehci_cap_regs_t *)controller.io_base;
    auto *op = (volatile ehci_op_regs_t *)(controller.io_base + cap->cap_length);
    const bool enabled = (op->command & EHCI_PSE) != 0;
    if (!wait_periodic_state(op, enabled, timer)) {
        controller.needs_reinit = true;
        return false;
    }
    op->command &= ~EHCI_PSE;
    (void)op->command;
    if (!wait_periodic_state(op, false, timer)) {
        controller.needs_reinit = true;
        return false;
    }
    ep.qh->horizontal_link = 1;
    if (!usb_periodic::install(Frame_list, 1024, plan.frames, 1, ep.qh_phys | 2u)) {
        if (enabled) op->command |= EHCI_PSE;
        return false;
    }
    asm volatile("mfence" ::: "memory");
    ep.scheduled = true;
    op->command |= EHCI_PSE;
    (void)op->command;
    if (!wait_periodic_state(op, true, timer)) {
        controller.needs_reinit = true;
        return false;
    }
    return true;
}

bool ehci_controller::unlink_interrupt_qh(ehci_endpoint_state &ep, TimerSource &timer) {
    auto *cap = (volatile ehci_cap_regs_t *)controller.io_base;
    auto *op = (volatile ehci_op_regs_t *)(controller.io_base + cap->cap_length);
    if (!wait_periodic_state(op, (op->command & EHCI_PSE) != 0, timer)) {
        controller.needs_reinit = true;
        return false;
    }
    op->command &= ~EHCI_PSE;
    (void)op->command;
    if (!wait_periodic_state(op, false, timer) ||
        !usb_periodic::remove(Frame_list, 1024, 1, ep.qh_phys | 2u)) {
        controller.needs_reinit = true;
        return false;
    }
    asm volatile("mfence" ::: "memory");
    ep.scheduled = false;
    return true; // Sole periodic request removed; leave PSE disabled.
}

bool ehci_controller::link_qh(ehci_endpoint_state &ep, TimerSource &timer) {
    if (!ep.qh || ep.scheduled || controller.needs_reinit ||
        (ep.transfer_type != 2 && ep.transfer_type != 3))
        return false;

    if (ep.transfer_type == 3) return link_interrupt_qh(ep, timer);

    volatile ehci_cap_regs_t *cap = (volatile ehci_cap_regs_t *)controller.io_base;
    volatile ehci_op_regs_t *op = (volatile ehci_op_regs_t *)(controller.io_base + cap->cap_length);


    bool enabled = (op->command & ASE) != 0;
    for (u32 elapsed = 0; elapsed < 100; elapsed++) { // 100 ms timeout
        if (((op->status & ASS) != 0) == enabled)
            break;

        wait_ms(timer, 1);
    }

    if (((op->status & ASS) != 0) != enabled)
        return false;

    op->command &= ~ASE;

    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        if (!(op->status & ASS))
            break;

        wait_ms(timer, 1);
    }

    if (op->status & ASS)
        return false;

    ep.qh->horizontal_link = async_head->horizontal_link;
    asm volatile("mfence" ::: "memory");
    async_head->horizontal_link = ep.qh_phys | 0x02u;
    ep.scheduled = true;

    asm volatile("mfence" ::: "memory");

    op->command |= ASE;

    for (u32 elapsed = 0; elapsed < 100; elapsed++) { // 100 ms timeout
        if (op->status & ASS)
            break;

        wait_ms(timer, 1);
    }

    if (!(op->status & ASS))
        return false;

    return true;
}


bool ehci_controller::unlink_qh(ehci_endpoint_state &ep, TimerSource &timer) {
    if (!ep.scheduled)
        return true;

    if (!async_head || !ep.qh ||
        (ep.transfer_type != 2 && ep.transfer_type != 3) || controller.needs_reinit)
        return false;

    if (ep.transfer_type == 3) return unlink_interrupt_qh(ep, timer);

    auto *cap = reinterpret_cast<volatile ehci_cap_regs_t *>(controller.io_base);
    auto *op = reinterpret_cast<volatile ehci_op_regs_t *>(controller.io_base + cap->cap_length);

    auto wait_state = [&](bool enabled) {
        for (u32 elapsed = 0; elapsed < 100; elapsed++) {
            if (((op->status & ASS) != 0) == enabled)
                return true;
            wait_ms(timer, 1);
        }
        return ((op->status & ASS) != 0) == enabled;
    };

    bool was_enabled = (op->command & ASE) != 0;
    if (!wait_state(was_enabled)) {
        controller.needs_reinit = true;
        return false;
    }

    op->command &= ~ASE;
    (void)op->command;

    if (!wait_state(false)) {
        controller.needs_reinit = true;
        return false;
    }

    volatile ehci_qh *previous = async_head;
    bool removed = false;

    for (u32 visited = 0; visited < 4096; ++visited) {
        u32 link = previous->horizontal_link;

        // Must be a valid QH pointer: T=0, Type=01.
        if ((link & 1u) || (link & 6u) != 2u)
            break;

        u32 next_phys = link & ~0x1fu;

        if (next_phys == ep.qh_phys) {
            previous->horizontal_link = ep.qh->horizontal_link;
            ep.scheduled = false;
            removed = true;
            break;
        }

        if (next_phys == async_head_phys)
            break; // Completed the circular list without finding it.

        u64 next_virtual = dma_phys_to_virt(next_phys);
        if (next_virtual == 0 || next_virtual == 1)
            break;

        previous =
            reinterpret_cast<volatile ehci_qh *>(next_virtual);
    }

    asm volatile("mfence" ::: "memory");

    if (!removed) {
        controller.needs_reinit = true;
        return false; // Leave the schedule disabled for recovery.
    }

    if (was_enabled)
        op->command |= ASE;

    (void)op->command;

    if (!wait_state(was_enabled)) {
        controller.needs_reinit = true;
        return false;
    }

    return true;
}


usb_transfer_result ehci_controller::start_transfer(u32 endpoint_address, void *buffer, u32 length, TimerSource &timer) {
    (void)timer;

    usb_transfer_result transfer = {0};

    transfer.controller_type = EHCI;
    transfer.controller_index = controller.index;

    transfer.endpoint_handle = endpoint_address;

    u8 port_index = (u8)((endpoint_address & 0xFF0000) >> 16);
    u8 interface_index = (u8)((endpoint_address & 0x00FF00) >> 8);
    u8 endpoint_index = (u8)(endpoint_address & 0x0000FF);

    if ((endpoint_address >> 24) || !ports || port_index >= ports_count) {
        transfer.status_code = FAILED;
        return transfer;
    }

    if (!ports[port_index].connected) {
        printf("EHCI: error: invalid device port 0x%b not connected\n", ports[port_index].port_number);
        transfer.status_code = NOT_CONNECTED;
        return transfer; // return 1 for failure
    }

    if (!ports[port_index].device || !ports[port_index].device->discovered) {
        printf("EHCI: error: the device on port 0x%b was not discovered\n", ports[port_index].port_number);
        transfer.status_code = NOT_DISCOVERED;
        return transfer;
    }

    u8 device_address = ports[port_index].device->address;
    ehci_address_state *state = nullptr;
    for (u8 i = 0; i < state_count; i++) {
        if (states[i].device_address == device_address)
            state = &states[i];
    }

    if (!state) {
        printf("EHCI: error: no matching address_state found for device_address 0x%b\n", device_address);
        transfer.status_code = NO_ADDRESS;
        return transfer;
    }

    if (!state->interfaces || interface_index >= state->interfaces_count ||
        !state->interfaces[interface_index].endpoints ||
        endpoint_index >= state->interfaces[interface_index].endpoint_count) {
        transfer.status_code = FAILED;
        return transfer;
    }

    ehci_endpoint_state &ep = state->interfaces[interface_index].endpoints[endpoint_index];

    if (controller.needs_reinit || ep.scheduled || ep.speed != USB_SPEED_HIGH || !ep.qh ||
        !ep.data_qtd || !ep.dummy_qtd || !ep.buffer ||
        ep.max_packet_size == 0 ||
        ep.max_packet_size > (ep.transfer_type == 3 ? 1024 : 512) ||
        !(ep.endpoint_address & 0x0fu)) {
        transfer.status_code = FAILED;
        return transfer;
    }

    if (length > ep.max_packet_size) {
        transfer.status_code = INVALID_LENGTH;
        return transfer;
    }

    if (length > 0 && !(ep.endpoint_address & 0x80u) && buffer == nullptr) {
        transfer.status_code = FAILED;
        return transfer;
    }

    // Split transactions and high-bandwidth endpoints are not supported.
    if ((ep.transfer_type != 2 && ep.transfer_type != 3) ||
        (ep.transfer_type == 3 && (ep.mult ||
         !usb_periodic::high_speed_plan(ep.interval).frames))) {
        transfer.status_code = FAILED;
        return transfer;
    }

    const bool in = (ep.endpoint_address & 0x80u) != 0;

    if (!in && length)
        memcpy(ep.buffer, buffer, length);

    // QH-managed toggle (DTC=0): preserve it across detached submissions.
    const u32 toggle = ep.qh->token & (1u << 31);
    *ep.qh = {};
    ep.qh->horizontal_link = 1;
    ep.qh->endpoint_characteristics =
        ((u32)ep.device_address & 0x7fu) |
        ((u32)(ep.endpoint_address & 0x0fu) << 8) |
        (2u << 12) | // EHCI high-speed encoding; not usb_speed's value.
        ((u32)ep.max_packet_size << 16);
    ep.qh->endpoint_capabilities = (1u << 30) |
        (ep.transfer_type == 3 ? usb_periodic::high_speed_plan(ep.interval).smask : 0);
    ep.qh->alternate_next_qtd = 1;
    ep.qh->token = toggle;

    *ep.dummy_qtd = {};
    ep.dummy_qtd->next_qtd = 1;
    ep.dummy_qtd->alternate_next_qtd = 1;

    *ep.data_qtd = {};
    ep.data_qtd->next_qtd = 1;
    ep.data_qtd->alternate_next_qtd = 1;
    ep.data_qtd->token =
        (1u << 7) | // Active.
        ((in ? 1u : 0u) << 8) |
        (3u << 10) | // CERR.
        (length << 16);

    if (length) {
        ep.data_qtd->buffer[0] = ep.buffer_phys;
        const u64 last = (u64)ep.buffer_phys + length - 1;
        for (u32 page = 1; page < 5; ++page) {
            const u64 address = ((u64)ep.buffer_phys & ~0xfffULL) +
                                ((u64)page << 12);
            if (address <= last)
                ep.data_qtd->buffer[page] = (u32)address;
        }
    }

    asm volatile("mfence" ::: "memory");
    ep.qh->next_qtd = ep.data_qtd_phys;
    asm volatile("mfence" ::: "memory");

    auto *cap = (volatile ehci_cap_regs_t *)controller.io_base;
    auto *op = (volatile ehci_op_regs_t *)(controller.io_base + cap->cap_length);
    async.endpoint = &ep;
    async.dma_buffer = ep.buffer;
    async.in = in;
    async.saved_command = op->command;
    transfer_phase(10); // Wait for a prior schedule transition, without sleeping.
    transfer.status_code = TRANSFER_PENDING;
    return transfer;
}

void ehci_controller::advance_transfer(TimerSource &timer) {
    auto &ep = *(ehci_endpoint_state *)async.endpoint;
    auto *cap = (volatile ehci_cap_regs_t *)controller.io_base;
    auto *op = (volatile ehci_op_regs_t *)(controller.io_base + cap->cap_length);
    const bool periodic = ep.transfer_type == 3;
    const u32 enable = periodic ? EHCI_PSE : ASE;
    const u32 running = periodic ? EHCI_PSS : ASS;
    auto fail = [&](int code) {
        controller.needs_reinit = true;
        transfer_complete(code);
    };
    if (controller.needs_reinit) { fail(FAILED); return; }
    switch (async.phase) {
    case 10:
        if (((op->status & running) != 0) != ((op->command & enable) != 0)) {
            if (phase_expired(timer, 100)) fail(LINK_ERROR);
            return;
        }
        op->command &= ~enable;
        (void)op->command;
        transfer_phase(1);
        return;
    case 1:
        if (op->status & running) {
            if (phase_expired(timer, 100)) fail(LINK_ERROR);
            return;
        }
        if (periodic) {
            const auto plan = usb_periodic::high_speed_plan(ep.interval);
            ep.qh->horizontal_link = 1;
            if (!usb_periodic::install(Frame_list, 1024, plan.frames, 1, ep.qh_phys | 2u)) {
                fail(LINK_ERROR);
                return;
            }
        } else {
            ep.qh->horizontal_link = async_head->horizontal_link;
            asm volatile("mfence" ::: "memory");
            async_head->horizontal_link = ep.qh_phys | 2u;
        }
        ep.scheduled = true;
        asm volatile("mfence" ::: "memory");
        op->command |= enable;
        (void)op->command;
        transfer_phase(2);
        return;
    case 2:
        if (!(op->status & running)) {
            if (phase_expired(timer, 100)) fail(LINK_ERROR);
            return;
        }
        transfer_phase(3);
        return;
    case 3:
        if (((volatile ehci_qtd *)ep.data_qtd)->token & (1u << 7)) {
            if (!async.cancel && !transfer_expired(timer)) return;
        }
        op->command &= ~enable;
        (void)op->command;
        transfer_phase(4);
        return;
    case 4:
        if (op->status & running) {
            if (phase_expired(timer, 100)) fail(UNLINK_ERROR);
            return;
        }
        if (periodic) {
            if (!usb_periodic::remove(Frame_list, 1024, 1, ep.qh_phys | 2u)) {
                fail(UNLINK_ERROR);
                return;
            }
        } else {
            volatile ehci_qh *previous = async_head;
            bool found = false;
            for (u32 i = 0; i < 4096; ++i) {
                u32 link = previous->horizontal_link;
                if ((link & 1u) || (link & 6u) != 2u) break;
                u32 phys = link & ~0x1fu;
                if (phys == ep.qh_phys) {
                    previous->horizontal_link = ep.qh->horizontal_link;
                    found = true;
                    break;
                }
                if (phys == async_head_phys) break;
                u64 address = dma_phys_to_virt(phys);
                if (!address || address == 1) break;
                previous = (volatile ehci_qh *)address;
            }
            if (!found) { fail(UNLINK_ERROR); return; }
        }
        ep.scheduled = false;
        asm volatile("mfence" ::: "memory");
        op->command = (op->command & ~enable) | (async.saved_command & enable);
        (void)op->command;
        transfer_phase(6);
        return;
    case 6: {
        if (((op->status & running) != 0) != ((async.saved_command & enable) != 0)) {
            if (phase_expired(timer, 100)) fail(UNLINK_ERROR);
            return;
        }
        const u32 token = ((volatile ehci_qtd *)ep.data_qtd)->token;
        async.result.hardware_status = token;
        if (token & (1u << 7)) {
            // Retain controller-owned DMA/toggle state for recovery.
            fail(async.cancel ? TRANSFER_CANCELLED : TIMEOUT);
        } else if (token & 0x7cu) {
            fail(FAILED);
        } else {
            const u32 remaining = (token >> 16) & 0x7fffu;
            if (remaining > async.length) { fail(INVALID_LENGTH); return; }
            async.result.actual_length = async.length - remaining;
            transfer_complete(TRANSFER_SUCCESS);
        }
        return;
    }
    default: fail(FAILED); return;
    }
}
