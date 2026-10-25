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

#define HCHalted (1u << 12) // HCHalted bit 12 of USBSTS
#define HCRESET (1u << 1) // USBCMD.HCRESET bit 1 

#define EHCI_PORTSC 0x44
#define CCS (1 << 0)

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

    u32 *Frame_list = (u32 *)PerFrameList.virtual_address;
    for (int i = 0; i < 1024; i++) 
        Frame_list[i] = 1; // fill all the 1024 periodic frame list entries with TERMINATE (1)
    
    PERIODICLISTBASE = (u32)PerFrameList.physical_address;

    USBCMD = (USBCMD & ~((1u << 4) | (1u << 5))) | 1u; // start EHCI with periodic and async schedules disabled
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

    u32 before = FRINDEX;
    wait_ms(timer, 1);
    u32 after = FRINDEX;
    if (before == after) {
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
        ports[port].connected = (status & CSS);
        ports[port].controller_type = EHCI;
    }

    return (int)ehci_port_count;
}