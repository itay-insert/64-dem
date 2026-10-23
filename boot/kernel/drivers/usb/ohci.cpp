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
    class_code = 0x0C:
    subclass = 0x03;
    prog_if = 0x10;
    type = MMIO;
    controller = {0};
    construct_controller();
}

#define HCCTRL ohci_regs->control
#define HCCMDSTS ohci_regs->command_status

#define InterruptRouting 0x80
#define OwnershipChangeRequest 0x04


int ohci_controller::controller_init(TimerSource &Timer) {
    volatile ohci_registers_t *ohci_regs = (volatile ohci_registers_t *)controller.io_base;

    if (HCCTRL & InterruptRouting) { // if bit 8 in hccontrol register is set, set bit 3 in the command status register
        HCCMDSTS |= OwnershipChangeRequest; // to take ownership from firmware
        u64 duration = Timer.read_freq() / 50000; // set 5 secs TIMEOUT duration
        u64 start = Timer.read();
        while (start - Timer.read() < duration) {
            if (!(HCCTRL & InterruptRouting))
                break;
        }
        printf("Kernel: obtained OHCI Ownership from firmware\n");
    }

    if (HCCTRL )
}