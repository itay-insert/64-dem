#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/ohci.hpp"
#include "drivers/display/vga.h"

typedef struct {
    u32 revision;             // 0x00 HcRevision
    u32 control;              // 0x04 HcControl
    u32 command_status;       // 0x08 HcCommandStatus
    u32 interrupt_status;    // 0x0C HcInterruptStatus
    u32 interrupt_enable;    // 0x10 HcInterruptEnable
    u32 interrupt_disable;   // 0x14 HcInterruptDisable
    u32 hcca;                 // 0x18 HcHCCA
    u32 periodic_current_ed;  // 0x1C HcPeriodCurrentED
    u32 control_head_ed;      // 0x20 HcControlHeadED
    u32 control_current_ed;   // 0x24 HcControlCurrentED
    u32 bulk_head_ed;         // 0x28 HcBulkHeadED
    u32 bulk_current_ed;      // 0x2C HcBulkCurrentED
    u32 done_head;            // 0x30 HcDoneHead
    u32 fm_interval;          // 0x34 HcFmInterval
    u32 fm_remaining;         // 0x38 HcFmRemaining
    u32 fm_number;            // 0x3C HcFmNumber
    u32 periodic_start;       // 0x40 HcPeriodicStart
    u32 ls_threshold;         // 0x44 HcLSThreshold
    u32 rh_descriptor_a;      // 0x48 HcRhDescriptorA
    u32 rh_descriptor_b;      // 0x4C HcRhDescriptorB
    u32 rh_status;            // 0x50 HcRhStatus
    u32 rh_port_status[15];   // 0x54 HcRhPortStatus[0..14]
} ohci_registers_t;

ohci_controller::ohci_controller() {
    memcpy(vendor, "OHCI", sizeof(vendor));
    class_code = 0x0C;
    subclass = 0x03;
    prog_if = 0x10;
    type = MMIO;
    controller = {0};
    construct_controller();
}

#define HCCTRL ohci_regs->control
#define HCCMDSTS ohci_regs->command_status
#define HCFS (HCCTRL >> 6)

#define InterruptRouting 0x100 // (bit 8)
#define OwnershipChangeRequest 0x08 // (bit 3)


#define USBRESET 0x00
#define USBRESUME 0x01
#define USBOPER 0x02
#define USBSUSP 0x03

#define CCS (1 << 0) // current connect status
#define PES (1 << 1) // port enable status
#define PRS (1 << 4) // port reset status
#define PRSC (1 << 20) // port reset change


#define RESET_PULSE 10
#define RESET_TIMEOUT 20
#define RESET_PULSES 5


int ohci_controller::controller_init(TimerSource &timer) {
    volatile ohci_registers_t *ohci_regs = (volatile ohci_registers_t *)controller.io_base;

    ohci_base = controller.io_base;
    if (HCCTRL & InterruptRouting) { // if bit 8 in hccontrol register is set, set bit 3 in the command status register
        HCCMDSTS |= OwnershipChangeRequest; // to take ownership from firmware
        int ms_target = 5000; // set 5 secs TIMEOUT duration
        int ms_count = 0;
        while (HCCTRL & InterruptRouting) {
            if (ms_count == ms_target)
                break;
            wait_ms(timer, 1);
            ms_count++;
        }

        if (!(HCCTRL & InterruptRouting))
            printf("Kernel: obtained OHCI Ownership from firmware\n");
        else {
            printf("Kernel: OHCI Ownership obtaining failed... aborting\n");
            return 1;
        }
    }

    u32 state = HCFS & 0x03; // get controller state from HCcontrol.HCFS
    switch (state) {
        case USBRESET:
            wait_ms(timer, 50); // wait 50 ms for USBRESET state
            break;

        case USBRESUME:
            wait_ms(timer, 20); // wait 20 ms for USBRESUME state
            break;

        case USBOPER:
            break;

        case USBSUSP:
            wait_ms(timer, 5); // wait 5 ms before switching to USBRESUME
            HCCTRL &= ~0x80;
            HCCTRL |= 0x40; // set HCCTRL.hcfs to USBRESUME by clearing bit 7 and setting bit 6
            (void)HCCTRL; // read back to flush MMIO write
            wait_ms(timer, 20); // wait 20 ms for USBRESUME state
            break;
        
    }

    ohci_regs->interrupt_disable = 0xC000007Fu; // disable interrupts by writing 0xC000007F to the interrupt_disable register

    dma_ret alloc = allocate_dma(256); // allocate the 256-byte HCCA
    if (alloc.status != 0) {
        printf("OHCI: failed to allocate the HCCA\n");
        return 1;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("OHCI: HCCA physical address too big!\n");
        return 1;
    }

    memset((void *)alloc.virtual_address, 0, 256); // zero-initialize the 256-byte HCCA
    HCCA_base = alloc.virtual_address;

    u32 HcFmInterval = ohci_regs->fm_interval; // save the HcFmInterval from the Fm_interval register

    const u64 timeout_ticks = (timer.read_freq() + 9999) / 10000; // divide by 10000 for a 100 us timeout
    const u32 start = (u32)timer.read() & 0x00FFFFFFu; // save lower 24 bits

    HCCMDSTS = 1u; // write 1 to CMDSTS register to request HCR

    while (HCCMDSTS & 1u) { // wait for HCR
        const u32 now = (u32)timer.read() & 0x00FFFFFFu; // save lower 24 bits
        if (((now - start) & 0x00FFFFFFu) >= timeout_ticks) {
            printf("OHCI: HCR reset timed out!\n");
            return 1;
        }
        __asm__ volatile("pause");
    }


    ohci_regs->fm_interval = HcFmInterval; // restoring HcFmInterval
    ohci_regs->hcca = alloc.physical_address; // writing the hcca physical address to the HCCA register

    ohci_regs->control_head_ed = 0; // set HCcontrol_head register to 0
    ohci_regs->bulk_head_ed = 0; // set bulk_head register to 0

    ohci_regs->periodic_start = ((HcFmInterval & 0x3FFFu) * 9) / 10; // set periodic_start

    HCCTRL |= 0x80;
    HCCTRL &= ~0x40; // set HCCTRL to USBOPER

    u16 before = ohci_regs->fm_number; 
    wait_ms(timer, 3);
    u16 after = ohci_regs->fm_number;

    if (before == after) {
        printf("OHCI: fm_number not advancing\n");
        return 1;
    }

    return 0;
}



int ohci_controller::enumrate_ports() {
    volatile ohci_registers_t *ohci_regs = (volatile ohci_registers_t *)controller.io_base;
    
    u8 ohci_port_count = (u8)(ohci_regs->rh_descriptor_a & 0xFF); // get the port count from the lower 8 bits of the rh_descriptor_a register
    ports = new usb_port_descriptor[ohci_port_count];
    if (ports == nullptr) // return -1 for allocation failure
        return -1;

    for (u8 port = 0; port < ohci_port_count; port++) {
        u32 status = ohci_read_port(port);
        ports[port].port_number = port + 1;
        ports[port].port_status = status;
        ports[port].connected = (status & CCS);
        ports[port].controller_type = OHCI;
    }

    return (int)ohci_port_count;

}

static inline u64 port_to_address(u8 port, u64 io_base) {
    return (io_base + 0x54 + ((port - 1) << 2));  // calculate the port_base address
}


int ohci_controller::reset_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;

    u64 port_base = port_to_address(port, controller.io_base);

    volatile u32 *port_reg = (volatile u32 *)port_base;

    u32 status = *port_reg;

    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;


    if (!(status & CCS))
        return NO_DEVICE; // no device connected on port


    bool idle = false; // wait for any reset in progress to finish

    for (u32 elapsed = 0; elapsed < RESET_TIMEOUT; elapsed++) {
        status = *port_reg;

        if (status == 0xFFFFFFFF)
            return INVALID_STATUS;

        if (!(status & CCS))
            return NO_DEVICE;

        if (!(status & PRS)) {
            idle = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!idle)
        return RESET_STILL_ASSERTED;

    
    // repeat the hardware pulse to provide 50 ms of reset signaling
    for (u32 pulse = 0; pulse < RESET_PULSES; pulse++) {
        status = *port_reg;

        if (status == 0xFFFFFFFF)
            return INVALID_STATUS;

        // clear a stale reset-completion flag
        // clear the W1C bit
        if (status & PRSC) 
            *port_reg = PRSC;

        // write the command bit to start reset
        *port_reg = PRS;


        bool completed = false;

        for (u32 elapsed = 0; elapsed < RESET_TIMEOUT; elapsed++) {
            wait_ms(timer, 1);
            status = *port_reg;

            if (status == 0xFFFFFFFF)
                return INVALID_STATUS;

            if (!(status & CCS))
                return NO_DEVICE;

            // require both reset deassert and the hardware reset-completion flag
            if (!(status & PRS) && (status & PRSC)) {
                completed = true;
                break;
            }
        }

        if (!completed)
            return RESET_FAILED;
    }

    wait_ms(timer, 10); // USB reset recovery period


    status = *port_reg;

    // verify the port's final state
    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (!(status & CCS))
        return NO_DEVICE;

    if (status & PRS)
        return RESET_STILL_ASSERTED;

    if (!(status & PES))
        return RESET_FAILED;

    return RESET_SUCCESS;
}


int ohci_controller::disable_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;

    u64 port_base = port_to_address(port, controller.io_base);
    volatile u32 *port_reg = (volatile u32 *)port_base;

    if (*port_reg == 0xFFFFFFFF)
        return INVALID_STATUS;

    *port_reg = CCS; // write CCS command to disable port
    (void)*port_reg; // read to flush MMIO write

    for (int i = 0; i < 100; i++) { // poll the status with 100 ms timeout
        if ((*port_reg & PES) == 0) 
            break;
        wait_ms(timer, 1);
    }

    return (*port_reg & PES) ? RESET_FAILED : RESET_SUCCESS; 
}