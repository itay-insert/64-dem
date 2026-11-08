#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/ohci.hpp"
#include "drivers/usb/periodic.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/kmalloc.h"

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
    controller_type = OHCI;
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


namespace ohci {
    namespace td {
        constexpr u32 condition_not_accessed = 0xFu << 28;
        constexpr u32 rounding               = 1u << 18;

        constexpr u32 setup = 0u << 19;
        constexpr u32 out   = 1u << 19;
        constexpr u32 in    = 2u << 19;

        constexpr u32 toggle_data0 = 2u << 24;
        constexpr u32 toggle_data1 = 3u << 24;

        constexpr u32 no_interrupt = 7u << 21;
        constexpr u32 interrupt    = 0u << 21;
    }

    namespace ed {
        constexpr u32 skip       = 1u << 14;
        constexpr u32 low_speed  = 1u << 13;
    }

    namespace command {
        constexpr u32 control_list_filled = 1u << 1;
    }

    namespace control {
        constexpr u32 control_list_enable = 1u << 4;
    }
}


constexpr u32 OHCI_ED_HEAD_HALTED = 1u << 0;
constexpr u32 OHCI_ED_HEAD_MASK   = 0xFFFFFFF0;


constexpr u32 OHCI_RHPS_LSDA = 1u << 9;

int ohci_controller::controller_init(TimerSource &timer) {
    states = nullptr;
    state_count = 0;

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
    hcca = (ohci_hcca *)HCCA_base;

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

#define OHCI_CLE (1u << 4)
#define OHCI_CLF (1u << 1)
#define OHCI_ED_SKIP (1u << 14)

#define OHCI_INTR_WDH (1u << 1)

#define OHCI_CC_NO_ERROR       0x0
#define OHCI_CC_CRC_ERROR      0x1
#define OHCI_CC_BIT_STUFFING   0x2
#define OHCI_CC_DATA_TOGGLE    0x3
#define OHCI_CC_STALL          0x4
#define OHCI_CC_DEV_NOT_RESP   0x5
#define OHCI_CC_PID_CHECK      0x6
#define OHCI_CC_UNEXPECTED_PID 0x7
#define OHCI_CC_DATA_OVERRUN   0x8
#define OHCI_CC_DATA_UNDERRUN  0x9
#define OHCI_CC_BUFFER_OVERRUN 0xC
#define OHCI_CC_BUFFER_UNDERRUN 0xD
#define OHCI_CC_NOT_ACCESSED   0xF

const char* ohci_condition_code_name(u32 cc) {
    switch (cc) {
        case OHCI_CC_NO_ERROR:
            return "No error";

        case OHCI_CC_CRC_ERROR:
            return "CRC error";

        case OHCI_CC_BIT_STUFFING:
            return "Bit stuffing error";

        case OHCI_CC_DATA_TOGGLE:
            return "Data toggle mismatch";

        case OHCI_CC_STALL:
            return "STALL";

        case OHCI_CC_DEV_NOT_RESP:
            return "Device not responding";

        case OHCI_CC_PID_CHECK:
            return "PID check failure";

        case OHCI_CC_UNEXPECTED_PID:
            return "Unexpected PID";

        case OHCI_CC_DATA_OVERRUN:
            return "Data overrun";

        case OHCI_CC_DATA_UNDERRUN:
            return "Data underrun";

        case OHCI_CC_BUFFER_OVERRUN:
            return "Buffer overrun";

        case OHCI_CC_BUFFER_UNDERRUN:
            return "Buffer underrun";

        case OHCI_CC_NOT_ACCESSED:
            return "Not accessed";

        default:
            return "Reserved condition code";
    }
}

void process_completed_td(u32 td_phys, const ohci_td* td)
{
    if (!td) {
        printf("OHCI: NULL completed TD\n");
        return;
    }

    u32 condition_code = (td->control >> 28) & 0xF;
    u32 error_count = (td->control >> 26) & 0x3;

    // Successful transfer.
    if (condition_code == OHCI_CC_NO_ERROR)
        return;

    printf(
        "OHCI: TD 0x%ux completed with CC=%u (%s), EC=%u\n",
        td_phys,
        condition_code,
        ohci_condition_code_name(condition_code),
        error_count
    );
}

struct ohci_transfer_watch {
    u32 setup_phys;
    u32 data_phys;
    u32 status_phys;

    // Bit 0 = SETUP retired
    // Bit 1 = DATA retired
    // Bit 2 = STATUS retired
    u8 retired_mask;
    usb_async_request *requests = nullptr;
    unsigned request_count = 0;
};

#define OHCI_TD_SETUP_DONE  (1u << 0)
#define OHCI_TD_DATA_DONE   (1u << 1)
#define OHCI_TD_STATUS_DONE (1u << 2)
#define OHCI_TD_ALL_DONE    0x07

#define OHCI_TD_CC_NOT_ACCESSED 0xF

struct ohci_done_entry {
    u32 phys;
    ohci_td* td;
};

bool process_done_queue(
    volatile ohci_hcca* hcca,
    volatile ohci_registers_t* regs,
    ohci_transfer_watch* watch = nullptr)
{
    // No published DoneHead queue to consume.
    if (!(regs->interrupt_status & OHCI_INTR_WDH))
        return true;

    // HC must not modify HccaDoneHead while WDH remains asserted.
    asm volatile("mfence" ::: "memory");

    u32 td_phys = hcca->done_head & 0xFFFFFFF0;

    ohci_done_entry entries[128];
    u32 count = 0;

    // First pass: validate and save the entire chain.
    while (td_phys != 0) {

        if (count >= 128) {
            printf("OHCI: DoneHead chain exceeded limit\n");
            return false;
        }

        u64 td_virt = dma_phys_to_virt(td_phys);

        if (td_virt == 0 || td_virt == 1) {
            printf("OHCI: Invalid DoneHead TD address\n");
            return false;
        }

        ohci_td* td = (ohci_td*)td_virt;

        entries[count].phys = td_phys;
        entries[count].td = td;
        count++;

        // HC overwrites NextTD with the DoneHead linkage.
        td_phys = td->next_td & 0xFFFFFFF0;
    }

    // Second pass: process the validated entries.
    for (u32 i = 0; i < count; i++) {

        u32 phys = entries[i].phys;
        ohci_td* td = entries[i].td;

        if (watch) {
            if (phys == watch->setup_phys)
                watch->retired_mask |= OHCI_TD_SETUP_DONE;

            else if (phys == watch->data_phys)
                watch->retired_mask |= OHCI_TD_DATA_DONE;

            else if (phys == watch->status_phys)
                watch->retired_mask |= OHCI_TD_STATUS_DONE;
        }

        if (watch && watch->requests) {
            for (unsigned j = 0; j < watch->request_count; ++j) {
                auto &request = watch->requests[j];
                if (request.active && !request.completed && request.endpoint &&
                    ((ohci_endpoint_state *)request.endpoint)->data_td_phys == phys)
                    request.retired |= OHCI_TD_DATA_DONE;
            }
        }

        process_completed_td(phys, td);
    }

    // Tell HC that software consumed the published queue.
    hcca->done_head = 0;

    asm volatile("mfence" ::: "memory");

    // WDH is write-one-to-clear.
    regs->interrupt_status = OHCI_INTR_WDH;

    return true;
}


enum class ohci_transfer_result {
    success,
    timeout,
    transfer_error
};

ohci_transfer_result wait_for_descriptor(
    volatile ohci_ed* ed,
    volatile ohci_td* setup_td,
    volatile ohci_td* data_td,
    volatile ohci_td* status_td,
    u32 dummy_phys,
    u32 data_buffer_phys,
    u32 data_length,
    ohci_hcca* hcca,
    volatile ohci_registers_t* regs,
    ohci_transfer_watch& watch,
    TimerSource& timer,
    u32& received_bytes
) {
    received_bytes = 0;

    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        
        // Process completed TDs whenever the HC writes back DoneHead
        if (regs->interrupt_status & OHCI_INTR_WDH)
            process_done_queue(hcca, regs, &watch);

        u32 headp = ed->headp;

        // The HC retired a TD with an error.
        if (headp & OHCI_ED_HEAD_HALTED)
            return ohci_transfer_result::transfer_error;

        // All three TDs have been retired successfully.
        if ((headp & OHCI_ED_HEAD_MASK) == dummy_phys) {

            u32 setup_cc = setup_td->control >> 28;
            u32 data_cc = 0;

            if (data_td)
                data_cc = data_td->control >> 28;
            u32 status_cc = status_td->control >> 28;

            if (setup_cc != 0 || (data_td && data_cc != 0) ||
                status_cc != 0) {
                return ohci_transfer_result::transfer_error;
            }

            if (data_td) {
                u32 cbp = data_td->cbp;

                if (cbp == 0) {
                    received_bytes = data_length;
                }
                else if (cbp >= data_buffer_phys &&
                         cbp <= data_buffer_phys + data_length - 1) {

                    received_bytes = cbp - data_buffer_phys;
                }
                else {
                    return ohci_transfer_result::transfer_error;
                }
            }
            return ohci_transfer_result::success;
        }

        wait_ms(timer, 1);
    }

    return ohci_transfer_result::timeout;
}


bool wait_for_next_frame(
    volatile u16& frame_number,
    u16 previous_frame,
    TimerSource& timer)
{
    for (u32 i = 0; i < 20; i++) {
        if (frame_number != previous_frame)
            return true;

        wait_ms(timer, 1);
    }

    return false;
}

bool stop_control_schedule(
    volatile ohci_registers_t* regs,
    volatile u16& frame_number,
    TimerSource& timer)
{
    u32 control = regs->control;

    // Already disabled: HC cannot process this list.
    if (!(control & OHCI_CLE))
        return true;

    // Capture the frame before disabling CLE.
    u16 previous_frame = frame_number;

    regs->control = control & ~OHCI_CLE;

    // Flush the MMIO write.
    volatile u32 readback = regs->control;
    (void)readback;

    asm volatile("mfence" ::: "memory");

    // Wait until disabling CLE has taken effect.
    return wait_for_next_frame(
        frame_number,
        previous_frame,
        timer
    );
}


bool wait_for_transfer_reap(
    volatile ohci_td* setup_td,
    volatile ohci_td* data_td,
    volatile ohci_td* status_td,
    volatile ohci_hcca* hcca,
    volatile ohci_registers_t* regs,
    ohci_transfer_watch* watch,
    TimerSource& timer)
{
    for (u32 elapsed = 0; elapsed < 20; elapsed++) {

        if (!process_done_queue(hcca, regs, watch))
            return false;

        u32 setup_cc = (setup_td->control >> 28) & 0xF;
        u32 data_cc = data_td ? (data_td->control >> 28) & 0xF
                              : OHCI_TD_CC_NOT_ACCESSED;
        u32 status_cc = (status_td->control >> 28) & 0xF;

        // A TD with CC=NotAccessed was never retired.
        // Otherwise, it must have appeared in DoneHead.
        bool setup_reaped =
            setup_cc == OHCI_TD_CC_NOT_ACCESSED ||
            (watch->retired_mask & OHCI_TD_SETUP_DONE);

        bool data_reaped =
            !data_td || data_cc == OHCI_TD_CC_NOT_ACCESSED ||
            (watch->retired_mask & OHCI_TD_DATA_DONE);

        bool status_reaped =
            status_cc == OHCI_TD_CC_NOT_ACCESSED ||
            (watch->retired_mask & OHCI_TD_STATUS_DONE);

        if (setup_reaped && data_reaped && status_reaped)
            return true;

        wait_ms(timer, 1);
    }

    // Final attempt.
    if (!process_done_queue(hcca, regs, watch))
        return false;

    return false;
}


bool restore_control_schedule(
    volatile ohci_registers_t* regs,
    volatile ohci_hcca* hcca,
    TimerSource& timer,
    volatile ohci_ed* ed,
    volatile ohci_td* setup_td,
    volatile ohci_td* data_td,
    volatile ohci_td* status_td,
    ohci_transfer_watch* watch,
    u32 old_control,
    u32 old_head,
    u32 old_current,
    bool old_clf)
{
    // Prevent additional transactions on the temporary ED.
    ed->control |= OHCI_ED_SKIP;

    asm volatile("mfence" ::: "memory");

    // Stop the control list and wait for CLE to take effect.
    if (!stop_control_schedule(
            regs, hcca->frame_number, timer)) {
        printf("OHCI: could not stop control schedule\n");
        return false;
    }

    // Make sure no retired TD from our allocation remains
    // linked in an unconsumed DoneHead queue.
    if (!wait_for_transfer_reap(
            setup_td,
            data_td,
            status_td,
            hcca,
            regs,
            watch,
            timer)) {
        printf("OHCI: could not reap transfer TDs\n");
        return false;
    }

    // CLF is a list-processing notification, not completion status.
    // Preserve any pending notification from either schedule.
    bool temporary_clf =
        (regs->command_status & OHCI_CLF) != 0;

    // CLE is disabled, so it is now safe to restore these pointers.
    regs->control_head_ed = old_head;
    regs->control_current_ed = old_current;

    asm volatile("mfence" ::: "memory");

    if (old_clf || temporary_clf)
        regs->command_status = OHCI_CLF;

    // Restore the original CLE state, preserving other control bits.
    u32 control = regs->control;

    control = (control & ~OHCI_CLE) |
              (old_control & OHCI_CLE);

    regs->control = control;

    volatile u32 readback = regs->control;
    (void)readback;

    asm volatile("mfence" ::: "memory");

    return true;
}


usb_device_descriptor ohci_controller::identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};

    dma_ret alloc = allocate_dma(96);
    if (alloc.status != 0 || alloc.physical_address > 0xFFFFFFFF) {
        printf("OHCI: setup packet dma allocation failed, either invalid address or allocation error\n");
        if (alloc.virtual_address)
            free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    ohci_ed* ed = (ohci_ed*)alloc.virtual_address;
    ohci_td* setup_td = (ohci_td*)(alloc.virtual_address + 16);
    ohci_td* data_td = (ohci_td*)(alloc.virtual_address + 32);
    ohci_td* status_td = (ohci_td*)(alloc.virtual_address + 48);
    ohci_td* dummy_td = (ohci_td*)(alloc.virtual_address + 64);

    usb_setup_packet* setup =
        (usb_setup_packet*)(alloc.virtual_address + 80);

    u8* descriptor_buffer =
        (u8*)(alloc.virtual_address + 88);

    *setup = {
        0x80,   // Device-to-host
        0x06,   // GET_DESCRIPTOR
        0x0100, // Device descriptor, index 0
        0x0000,
        8
    };

    u32 ed_phys = (u32)alloc.physical_address;
    u32 setup_phys = ed_phys + 16;
    u32 data_phys = ed_phys + 32;
    u32 status_phys = ed_phys + 48;
    u32 dummy_phys = ed_phys + 64;
    u32 setup_buffer_phys = ed_phys + 80;
    u32 descriptor_buffer_phys = ed_phys + 88;

    ed->control =
        (0u << 0)  |     // Function address 0
        (0u << 7)  |     // Endpoint number 0
        (0u << 11) |     // Direction from TD
        (8u << 16);      // Maximum packet size 8

    volatile u32 *portreg = (volatile u32 *)port_to_address(port, controller.io_base);
    bool device_is_low_speed = (*portreg & OHCI_RHPS_LSDA) != 0;
    if (device_is_low_speed)
        ed->control |= ohci::ed::low_speed;

    ed->tailp = dummy_phys;
    ed->headp = setup_phys;
    ed->next_ed = 0;

    setup_td->control =
    ohci::td::condition_not_accessed |
    ohci::td::setup |
    ohci::td::toggle_data0;

    setup_td->cbp = setup_buffer_phys;
    setup_td->next_td = data_phys;
    setup_td->be = setup_buffer_phys + 7;


    data_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::rounding |
        ohci::td::in |
        ohci::td::toggle_data1;

    data_td->cbp = descriptor_buffer_phys;
    data_td->next_td = status_phys;
    data_td->be = descriptor_buffer_phys + 7;


    status_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::out |
        ohci::td::toggle_data1;

    status_td->cbp = 0;
    status_td->next_td = dummy_phys;
    status_td->be = 0;


    // Dummy tail TD
    dummy_td->control = 0;
    dummy_td->cbp = 0;
    dummy_td->next_td = 0;
    dummy_td->be = 0;


    volatile ohci_registers_t *ohci_regs = (volatile ohci_registers_t *)controller.io_base;
    
    u32 old_control = ohci_regs->control;

    if (!stop_control_schedule(ohci_regs, hcca->frame_number, timer)) {
        printf("OHCI: timeout disabling control list\n");

        ohci_regs->control = old_control;

        volatile u32 readback = ohci_regs->control;
        (void)readback;

        controller.needs_reinit = true;

        free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    u32 old_head = ohci_regs->control_head_ed;
    u32 old_current = ohci_regs->control_current_ed;


    bool old_clf = (ohci_regs->command_status & OHCI_CLF) != 0;

    ohci_regs->control_head_ed = ed_phys;

    ohci_regs->control_current_ed = 0;


    ohci_regs->command_status = OHCI_CLF;

    asm volatile("mfence" ::: "memory");

    ohci_regs->control = old_control | OHCI_CLE;

    ohci_transfer_watch watch = {
        setup_phys,
        data_phys,
        status_phys,
        0
    };


    u32 received = 0;

    auto result = wait_for_descriptor(
        ed,
        setup_td,
        data_td,
        status_td,
        dummy_phys,
        descriptor_buffer_phys,
        8,
        hcca,
        ohci_regs,
        watch,
        timer,
        received
    );

    if (!restore_control_schedule(
        ohci_regs,
        hcca,
        timer,
        ed,
        setup_td,
        data_td,
        status_td,
        &watch,
        old_control,
        old_head,
        old_current,
        old_clf)) {

        printf("OHCI: controller recovery failed\n");

        controller.needs_reinit = true;

        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;
        
        *new_cache.error_code = (OHCI << 8) | NEEDS_REINIT;
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

    if (result != ohci_transfer_result::success) {
        printf("OHCI: device descriptor transfer error\n");
        free_dma(alloc.virtual_address);
        return device_descriptor;
    }

    usb_device_descriptor *desc = (usb_device_descriptor *)(((u64)descriptor_buffer_phys - alloc.physical_address) + alloc.virtual_address);

    if (received == 8 && desc->bLength == 18 && desc->bDescriptorType == 1) {
        usb_device_descriptor *device_ptr = &device_descriptor;
        memcpy(device_ptr, desc, 8);
    }

    free_dma(alloc.virtual_address);
    return device_descriptor;
}



static u8 next_usb_address = 1;

static inline u8 allocate_usb_address(void) {
    if (next_usb_address > 127) 
        return 0;  // no addresses left

    return next_usb_address++;
}



usb_device ohci_controller::set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port) {
    usb_device device = {0};
    if (desc.bLength != 18 || desc.bDescriptorType != 1 ||
        (desc.bMaxPacketSize0 != 8 && desc.bMaxPacketSize0 != 16 &&
         desc.bMaxPacketSize0 != 32 && desc.bMaxPacketSize0 != 64))
        return device;

    u64 size = sizeof(ohci_ed) + (sizeof(ohci_td) * 3) + 8;

    dma_ret alloc = allocate_dma(size);
    if (alloc.status != 0) {
        printf("OHCI: set address allocation failed\n");
        device.discovered = false;
        return device;
    }
    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("OHCI: set address physical address too big\n");
        device.discovered = false;
        return device;
    }

    u32 ed_phys = (u32)alloc.physical_address;
    u32 setup_td_phys = ed_phys + sizeof(ohci_ed);
    u32 status_phys = setup_td_phys + sizeof(ohci_td);
    u32 dummy_phys = status_phys + sizeof(ohci_td);
    u32 setup_phys = dummy_phys + sizeof(ohci_td);

    ohci_ed *ed = (ohci_ed *)alloc.virtual_address;
    ohci_td *setup_td = (ohci_td *)(alloc.virtual_address + sizeof(ohci_ed));
    ohci_td *status_td = (ohci_td *)((u8 *)setup_td + sizeof(ohci_td));
    ohci_td *dummy_td = (ohci_td *)((u8 *)status_td + sizeof(ohci_td));
    usb_setup_packet *setup =
    (usb_setup_packet *)((u8 *)dummy_td + sizeof(ohci_td));

    u8 address = allocate_usb_address();
    if (!address) {
        free_dma(alloc.virtual_address);
        return device;
    }

    *setup = {
        .BmRequestType = 0x00,
        .bRequest      = 0x05,
        .wValue        = address,
        .wIndex        = 0,
        .wLength       = 0
    };

    u32 max_packet_size = desc.bMaxPacketSize0;

    volatile u32 *portreg =
        (volatile u32 *)port_to_address(port, controller.io_base);

    bool low_speed = (*portreg & OHCI_RHPS_LSDA) != 0;

    if (low_speed && max_packet_size != 8) {
        device.discovered = false;
        free_dma(alloc.virtual_address);
        return device;
    }

    if (!low_speed &&
        max_packet_size != 8 &&
        max_packet_size != 16 &&
        max_packet_size != 32 &&
        max_packet_size != 64) {
            device.discovered = false;
            free_dma(alloc.virtual_address);
            return device;
    }

    *ed = {};

    ed->control =
        (0u << 0) |                  // Function address 0
        (0u << 7) |                  // Endpoint 0
        (0u << 11) |                 // Direction from TD
        (low_speed ? ohci::ed::low_speed : 0) |
        (max_packet_size << 16);

    ed->tailp = dummy_phys;
    ed->headp = setup_td_phys;
    ed->next_ed = 0;

    *setup_td = {};

    setup_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::setup |
        ohci::td::toggle_data0;

    setup_td->cbp = setup_phys;
    setup_td->next_td = status_phys;
    setup_td->be = setup_phys + 7;

    *status_td = {};

    status_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::in |
        ohci::td::toggle_data1;

    status_td->cbp = 0;
    status_td->next_td = dummy_phys;
    status_td->be = 0;

    *dummy_td = {};

    volatile ohci_registers_t *ohci_regs =
    (volatile ohci_registers_t *)controller.io_base;

    u32 old_control = ohci_regs->control;

    if (!stop_control_schedule(
            ohci_regs, hcca->frame_number, timer)) {

        printf("OHCI: timeout disabling control list\n");

        ohci_regs->control = old_control;
        volatile u32 readback = ohci_regs->control;
        (void)readback;

        controller.needs_reinit = true;
        free_dma(alloc.virtual_address);
        return device;
    }

    u32 old_head = ohci_regs->control_head_ed;
    u32 old_current = ohci_regs->control_current_ed;

    bool old_clf =
        (ohci_regs->command_status & OHCI_CLF) != 0;

    // Install our ED.
    ohci_regs->control_head_ed = ed_phys;
    ohci_regs->control_current_ed = 0;

    // Tell OHCI that the control list has new work.
    ohci_regs->command_status = OHCI_CLF;

    asm volatile("mfence" ::: "memory");

    // Enable the control list.
    ohci_regs->control = old_control | OHCI_CLE;

    ohci_transfer_watch watch = {
        setup_td_phys,
        0,
        status_phys,
        0
    };

    u32 received = 0;

    auto result = wait_for_descriptor(ed, setup_td, nullptr, status_td, dummy_phys, 0, 0, hcca, ohci_regs, watch, timer, received);
    if (!restore_control_schedule(
        ohci_regs,
        hcca,
        timer,
        ed,
        setup_td,
        nullptr,
        status_td,
        &watch,
        old_control,
        old_head,
        old_current,
        old_clf)) {

        printf("OHCI: controller recovery failed\n");

        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;

        *new_cache.dma = alloc;
        *new_cache.error_code = (OHCI << 8) | RESTORATION_FAILURE;

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

    if (result != ohci_transfer_result::success) {
        printf("OHCI: SET ADDRESS failed\n");
        device.discovered = false;

        
        next_usb_address = address;
        
        free_dma(alloc.virtual_address);
        return device;
    }

    wait_ms(timer, 2);

    device.address = address;

    free_dma(alloc.virtual_address);

    size = sizeof(ohci_ed) + (sizeof(ohci_td) * 4) + 8 + 18;

    alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("OHCI: GET DESCRIPTOR allocation failed\n");
        device.discovered = false;
        return device;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("OHCI: GET DESCRIPTOR physical address too big\n");
        device.discovered = false;
        return device;
    }

    ed_phys = (u32)alloc.physical_address;
    setup_td_phys = ed_phys + sizeof(ohci_ed);
    u64 data_td_phys = setup_td_phys + sizeof(ohci_td);
    status_phys = data_td_phys + sizeof(ohci_td);
    dummy_phys = status_phys + sizeof(ohci_td);
    setup_phys = dummy_phys + sizeof(ohci_td);
    u64 desc_phys = setup_phys + 8;

    ed = (ohci_ed *)alloc.virtual_address;
    setup_td = (ohci_td *)(alloc.virtual_address + sizeof(ohci_ed));
    ohci_td *data_td = (ohci_td *)((u8 *)setup_td + sizeof(ohci_td));
    status_td = (ohci_td *)((u8 *)data_td + sizeof(ohci_td));
    dummy_td = (ohci_td *)((u8 *)status_td + sizeof(ohci_td));
    setup = (usb_setup_packet *)((u8 *)dummy_td + sizeof(ohci_td));

    *setup = {
        .BmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue = 0x0100,
        .wIndex = 0,
        .wLength = 18
    };

   
    if (low_speed && max_packet_size != 8) {
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    if (!low_speed &&
        max_packet_size != 8 &&
        max_packet_size != 16 &&
        max_packet_size != 32 &&
        max_packet_size != 64) {
        free_dma(alloc.virtual_address);
        device.discovered = false;
        return device;
    }

    *ed = {};

    ed->control =
        ((u32)address << 0) |
        (0u << 7) |
        (0u << 11) |
        (low_speed ? ohci::ed::low_speed : 0) |
        (max_packet_size << 16);

    ed->tailp = dummy_phys;
    ed->headp = setup_td_phys;
    ed->next_ed = 0;

    *setup_td = {};

    setup_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::setup |
        ohci::td::toggle_data0;

    setup_td->cbp = setup_phys;
    setup_td->next_td = data_td_phys;
    setup_td->be = setup_phys + 7;

    *data_td = {};

    data_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::rounding |
        ohci::td::in |
        ohci::td::toggle_data1;

    data_td->cbp = desc_phys;
    data_td->next_td = status_phys;
    data_td->be = desc_phys + 17;

    *status_td = {};

    status_td->control =
        ohci::td::condition_not_accessed |
        ohci::td::out |
        ohci::td::toggle_data1;

    status_td->cbp = 0;
    status_td->next_td = dummy_phys;
    status_td->be = 0;

    *dummy_td = {};

    old_control = ohci_regs->control;

    if (!stop_control_schedule(
            ohci_regs, hcca->frame_number, timer)) {

        printf("OHCI: timeout disabling control list\n");

        ohci_regs->control = old_control;
        volatile u32 readback = ohci_regs->control;
        (void)readback;

        controller.needs_reinit = true;
        free_dma(alloc.virtual_address);
        return device;
    }

    old_head = ohci_regs->control_head_ed;
    old_current = ohci_regs->control_current_ed;

    old_clf =
        (ohci_regs->command_status & OHCI_CLF) != 0;

    // Install our ED.
    ohci_regs->control_head_ed = ed_phys;
    ohci_regs->control_current_ed = 0;

    // Tell OHCI that the control list has new work.
    ohci_regs->command_status = OHCI_CLF;

    asm volatile("mfence" ::: "memory");

    // Enable the control list.
    ohci_regs->control = old_control | OHCI_CLE;

    watch = {
        setup_td_phys,
        (u32)data_td_phys,
        status_phys,
        0
    };

    received = 0;
    result = wait_for_descriptor(
        ed,
        setup_td,
        data_td,
        status_td,
        dummy_phys,
        desc_phys,
        18,                // Request length
        hcca,
        ohci_regs,
        watch,
        timer,
        received
    );

    if (!restore_control_schedule(
        ohci_regs,
        hcca,
        timer,
        ed,
        setup_td,
        data_td,
        status_td,
        &watch,
        old_control,
        old_head,
        old_current,
        old_clf)) {

        printf("OHCI: controller recovery failed\n");

        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;

        *new_cache.dma = alloc;
        *new_cache.error_code = (OHCI << 8) | RESTORATION_FAILURE;

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


    if (result != ohci_transfer_result::success) {
        printf("OHCI: GET DESCRIPTOR failed\n");
        device.discovered = false;

        int rests = reset_port(port, timer);
        if (rests != 0) {
            printf("OHCI: failed to reset port 0x%b\n", port);
        } else {
            next_usb_address = address;
        }
        free_dma(alloc.virtual_address);
        device.address = 0;
        return device;
    } 

    usb_device_descriptor *desc_ptr = (usb_device_descriptor *)((u8 *)setup + 8);
    bool valid_descriptor = received == 18 &&
        desc_ptr->bDescriptorType == 0x01 && desc_ptr->bLength == 18;
    if (valid_descriptor) {
        memcpy(&desc, desc_ptr, 18);
    }

    device.descriptor = desc;
    device.discovered = valid_descriptor;
    free_dma(alloc.virtual_address);
    return device;
}


// ========================================
// OHCI Endpoint Descriptor (ED)
// ========================================

// ED info
#define OHCI_ED_FA(addr)       (((u32)(addr) & 0x7F) << 0)
#define OHCI_ED_EN(ep)         (((u32)(ep) & 0x0F) << 7)
#define OHCI_ED_D(dir)         (((u32)(dir) & 0x03) << 11)
#define OHCI_ED_LS             (1u << 13)
#define OHCI_ED_MPS(size)      (((u32)(size) & 0x7FF) << 16)

// ED direction
#define OHCI_ED_DIR_FROM_TD    0
#define OHCI_ED_DIR_OUT        1
#define OHCI_ED_DIR_IN         2

// ED HeadP flags
#define OHCI_ED_HEAD_HALTED    (1u << 0)
#define OHCI_ED_HEAD_CARRY     (1u << 1)
#define OHCI_ED_HEAD_MASK      0xFFFFFFF0u

// ED pointers
#define OHCI_ED_PTR_MASK       0xFFFFFFF0u


// ========================================
// OHCI Transfer Descriptor (General TD)
// ========================================

// TD INFO
#define OHCI_TD_CC(cc)         (((u32)(cc) & 0x0F) << 28)
#define OHCI_TD_EC(ec)         (((u32)(ec) & 0x03) << 26)
#define OHCI_TD_T(toggle)      (((u32)(toggle) & 0x03) << 24)
#define OHCI_TD_DP(dir)        (((u32)(dir) & 0x03) << 19)
#define OHCI_TD_DI(di)         (((u32)(di) & 0x07) << 21)
#define OHCI_TD_R              (1u << 18)

// TD condition codes
#define OHCI_TD_CC_NO_ERROR    0x0
#define OHCI_TD_CC_CRC         0x1
#define OHCI_TD_CC_BITSTUFF    0x2
#define OHCI_TD_CC_TOGGLE      0x3
#define OHCI_TD_CC_STALL       0x4
#define OHCI_TD_CC_NO_RESPONSE 0x5
#define OHCI_TD_CC_PID_CHECK   0x6
#define OHCI_TD_CC_UNEXPECTED  0x7
#define OHCI_TD_CC_DATA_OVERRUN 0x8
#define OHCI_TD_CC_DATA_UNDERRUN 0x9
#define OHCI_TD_CC_BUFFER_OVERRUN 0xC
#define OHCI_TD_CC_BUFFER_UNDERRUN 0xD
#define OHCI_TD_CC_NOT_ACCESSED 0xF

// TD toggle values
#define OHCI_TD_T_DATA0        0x02
#define OHCI_TD_T_DATA1        0x03
#define OHCI_TD_T_CARRY        0x00

// TD direction / PID values
#define OHCI_TD_DP_SETUP       0x00
#define OHCI_TD_DP_OUT         0x01
#define OHCI_TD_DP_IN          0x02

// TD buffer rounding
#define OHCI_TD_R_ROUNDING     OHCI_TD_R
#define OHCI_TD_R_NO_ROUNDING  0


// ========================================
// OHCI General TD pointers
// ========================================

#define OHCI_TD_PTR_MASK       0xFFFFFFF0u

// ========================================
// OHCI Control
// ========================================

#define OHCI_CONTROL_CLE       (1u << 4)
#define OHCI_CONTROL_BLE       (1u << 5)
#define OHCI_CONTROL_PLE       (1u << 2)
#define OHCI_CONTROL_IE        (1u << 3)



void *ohci_controller::usb_send_packet(usb_setup_packet packet, u32 maxpacketsize, u64 length, u8 address, TimerSource &timer, u8 port) {
    if (!prepare_control(timer)) return nullptr;

    volatile ohci_registers_t *ohci_regs = (volatile ohci_registers_t *)controller.io_base;

    if (!maxpacketsize || length != packet.wLength || length > 4096 ||
        (length && !(packet.BmRequestType & IN)))
        return nullptr;

    // Keep each IN TD within one USB packet. A rounded TD can retire on a
    // short response; chaining packet-sized TDs preserves the control stage.
    u32 data_td_count = (length + maxpacketsize - 1u) / maxpacketsize;
    u64 size = sizeof(ohci_ed) + ((data_td_count + 3) * sizeof(ohci_td)) + 8 + length;

    dma_ret alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("OHCI: packet allocation failed\n");
        return nullptr;
    }

    if (alloc.physical_address > 0xFFFFFFFF) {
        printf("OHCI: packet physical address too big!\n");
        free_dma(alloc.virtual_address);
        return nullptr;
    }    
    
    volatile u32 *portreg =
        (volatile u32 *)port_to_address(port, controller.io_base);

    bool low_speed = (*portreg & OHCI_RHPS_LSDA) != 0;

    u32 ed_phys = (u32)alloc.physical_address;
    u32 setup_td_phys = ed_phys + sizeof(ohci_ed);
    u32 data_td_phys = setup_td_phys + sizeof(ohci_td);
    u32 status_td_phys = data_td_phys + (data_td_count * sizeof(ohci_td));
    u32 dummy_phys = status_td_phys + sizeof(ohci_td);
    u32 setup_phys = dummy_phys + sizeof(ohci_td);
    u32 data_phys = setup_phys + sizeof(usb_setup_packet);

    ohci_ed *ed = (ohci_ed *)alloc.virtual_address;
    ohci_td *setup_td = (ohci_td *)(alloc.virtual_address + sizeof(ohci_ed));
    ohci_td *data_td = nullptr;

    if (data_td_count)
        data_td = (ohci_td *)((u8 *)setup_td + sizeof(ohci_td));
    ohci_td *status_td = (ohci_td *)((u8 *)setup_td + sizeof(ohci_td) + (data_td_count * sizeof(ohci_td)));
    ohci_td *dummy_td = (ohci_td *)((u8 *)status_td + sizeof(ohci_td));
    usb_setup_packet *setup = (usb_setup_packet *)((u8 *)dummy_td + sizeof(ohci_td));

    *setup = packet;

    *setup_td = {};
    setup_td->next_td = data_td ? data_td_phys : status_td_phys;

    setup_td->control =
        OHCI_TD_CC(0xF) |
        OHCI_TD_EC(0) |
        OHCI_TD_T(OHCI_TD_T_DATA0) |
        OHCI_TD_DP(OHCI_TD_DP_SETUP);

    setup_td->cbp = setup_phys;
    setup_td->be = setup_phys + 7;

    bool data_in = setup->BmRequestType & 0x80;

    u32 offset = 0;

    for (u32 i = 0; i < data_td_count; i++) {
        u32 remaining = length - offset;

        u32 chunk_length =
            remaining < maxpacketsize
                ? remaining
                : maxpacketsize;

        bool last = (i == data_td_count - 1);

        // Clear descriptor
        data_td[i] = {};

        // The first control DATA TD starts with DATA1.
        // Subsequent TDs inherit the toggle from the ED.
        u32 toggle = (i == 0)
            ? OHCI_TD_T_DATA1
            : OHCI_TD_T_CARRY;

        data_td[i].control =
            OHCI_TD_CC(OHCI_TD_CC_NOT_ACCESSED) |
            OHCI_TD_EC(0) |
            OHCI_TD_T(toggle) |
            OHCI_TD_DP(data_in
                ? OHCI_TD_DP_IN
                : OHCI_TD_DP_OUT) |
            ((data_in && last) ? OHCI_TD_R : 0);

        // Data buffer
        data_td[i].cbp = data_phys + offset;
        data_td[i].be = data_phys + offset + chunk_length - 1;

        // Next TD
        data_td[i].next_td =
            last
                ? status_td_phys
                : data_td_phys + (i + 1) * sizeof(ohci_td);

        offset += chunk_length;
    }

    *status_td = {};

    bool status_in = setup->wLength == 0 || !data_in;

    status_td->control =
        OHCI_TD_CC(0xF) |
        OHCI_TD_EC(0) |
        OHCI_TD_T(OHCI_TD_T_DATA1) |
        OHCI_TD_DP(status_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT);

    status_td->cbp = 0;
    status_td->be = 0;
    status_td->next_td = dummy_phys;


    *dummy_td = {};

    dummy_td->control = OHCI_TD_CC(0xF);
    dummy_td->cbp = 0;
    dummy_td->be = 0;
    dummy_td->next_td = 0;


    *ed = {};

    ed->control =
        OHCI_ED_FA(address) |
        OHCI_ED_EN(0) |
        OHCI_ED_D(OHCI_ED_DIR_FROM_TD) |
        (low_speed ? OHCI_ED_LS : 0) |
        OHCI_ED_MPS(maxpacketsize);

    ed->next_ed = 0;
    ed->tailp = dummy_phys;
    ed->headp = setup_td_phys;
    
    u32 old_control = ohci_regs->control;

    if (!stop_control_schedule(
            ohci_regs, hcca->frame_number, timer)) {

        printf("OHCI: timeout disabling control list\n");

        ohci_regs->control = old_control;
        volatile u32 readback = ohci_regs->control;
        (void)readback;

        controller.needs_reinit = true;
        free_dma(alloc.virtual_address);
        return nullptr;
    }

    u32 old_head = ohci_regs->control_head_ed;
    u32 old_current = ohci_regs->control_current_ed;

    bool old_clf =
        (ohci_regs->command_status & OHCI_CLF) != 0;

    // Install our ED.
    ohci_regs->control_head_ed = ed_phys;
    ohci_regs->control_current_ed = 0;

    // Tell OHCI that the control list has new work.
    ohci_regs->command_status = OHCI_CLF;

    asm volatile("mfence" ::: "memory");

    // Enable the control list.
    ohci_regs->control = old_control | OHCI_CLE;

    ohci_transfer_watch watch = {
        setup_td_phys,
        data_td ? data_td_phys : 0u,
        status_td_phys,
        0
    };

    watch.requests = requests;
    watch.request_count = request_capacity;
    u32 received = 0;
    auto result = wait_for_descriptor(
        ed,
        setup_td,
        data_td,
        status_td,
        dummy_phys,
        data_phys,
        (u32)length,
        hcca,
        ohci_regs,
        watch,
        timer,
        received
    );

    if (!restore_control_schedule(
        ohci_regs,
        hcca,
        timer,
        ed,
        setup_td,
        data_td,
        status_td,
        &watch,
        old_control,
        old_head,
        old_current,
        old_clf)) {

        printf("OHCI: controller recovery failed\n");

        controller.needs_reinit = true;
        
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[1];
        new_cache.error_code = new u16[1];
        new_cache.max = 1;

        *new_cache.dma = alloc;
        *new_cache.error_code = (OHCI << 8) | RESTORATION_FAILURE;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;

        controller.cached.cache = cache;
        controller.cached.max++;

        
        return nullptr;
    }


    if (result != ohci_transfer_result::success) {
        printf("OHCI: control request %u on port %u failed, "
               "CC=%u/%u/%u, MPS=%u\n",
               setup->bRequest, port,
               (setup_td->control >> 28) & 15u,
               data_td ? ((data_td->control >> 28) & 15u) : 0u,
               (status_td->control >> 28) & 15u,
               maxpacketsize);
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

#define OHCI_PORT_LSDA (1u << 9)

static inline ohci_address_state *allocate_state(ohci_address_state *states, u8 state_count, usb_device device) {
    if (state_count == 0) {
        states = new ohci_address_state[1];
        states->interfaces = nullptr;
        states->device_address = device.address;
        states->interfaces_count = device.usb_interface_count;
        return states;
    } // if state_count is 0, allocate states

    u64 old_size = state_count * sizeof(ohci_address_state);
    u64 new_size = old_size + sizeof(ohci_address_state);
    states = (ohci_address_state *)krealloc(states, old_size, new_size); // else increase the former allocation
    states[state_count].interfaces = nullptr;
    states[state_count].interfaces_count = device.usb_interface_count;
    states[state_count].device_address = device.address;
    return states;
}

ohci_endpoint_state ohci_controller::configure_endpoint(usb_device device, u16 endpoint_index, u16 interface_index, TimerSource &timer) {
    ohci_endpoint_state endpoint = {0};

    u64 size = sizeof(ohci_ed) + (sizeof(ohci_td) << 1) + device.interfaces[interface_index].endpoints[endpoint_index].max_packet_size;
    dma_ret alloc = allocate_dma(size);

    if (alloc.status != 0) {
        printf("OHCI: configure_endpoint allocation error\n");
        endpoint.dma = alloc;
        return endpoint;
    }

    if (alloc.physical_address >= 0x100000000ULL ||
        size > 0x100000000ULL - alloc.physical_address) {
        printf("OHCI: configure_endpoint allocation physical address too big\n");
        free_dma(alloc.virtual_address);
        alloc.status = 1;
        endpoint.dma = alloc;
        return endpoint;
    }

    endpoint.dma = alloc;
    endpoint.max_packet_size = device.interfaces[interface_index].endpoints[endpoint_index].max_packet_size;
    endpoint.interval = device.interfaces[interface_index].endpoints[endpoint_index].interval;
    endpoint.device_address = device.address;
    endpoint.endpoint_address = device.interfaces[interface_index].endpoints[endpoint_index].address;


    volatile u32 *port_reg = (volatile u32 *)port_to_address(device.port_number, controller.io_base);
    u32 status = *port_reg;

    endpoint.low_speed = status & OHCI_PORT_LSDA;
    endpoint.scheduled = false;

    endpoint.transfer_type = device.interfaces[interface_index].endpoints[endpoint_index].transfer_type;

    u32 ed_phys = (u32)alloc.physical_address;
    u32 dummy_td_phys = ed_phys + sizeof(ohci_ed);
    endpoint.data_td_phys = dummy_td_phys + sizeof(ohci_td);
    endpoint.buffer_phys = dummy_td_phys + (sizeof(ohci_td) << 1);

    ohci_ed *ed = (ohci_ed *)alloc.virtual_address;
    ohci_td *dummy_td = (ohci_td *)(alloc.virtual_address + sizeof(ohci_ed));
    endpoint.data_td = (ohci_td *)((u8 *)dummy_td + sizeof(ohci_td));
    endpoint.buffer = (void *)((u8 *)dummy_td + (sizeof(ohci_td) << 1));

    endpoint.ed_phys = ed_phys;
    endpoint.dummy_td_phys = dummy_td_phys;

    endpoint.ed = ed;
    endpoint.dummy_td = dummy_td;

    *ed = {};
    *dummy_td = {};

    const bool in = (endpoint.endpoint_address & 0x80u) != 0;

    ed->control =
        ((u32)endpoint.device_address & 0x7fu) |
        ((u32)(endpoint.endpoint_address & 0x0fu) << 7) |
        ((in ? 2u : 1u) << 11) |
        (endpoint.low_speed ? ohci::ed::low_speed : 0) |
        ohci::ed::skip |
        ((u32)endpoint.max_packet_size << 16);

    ed->headp = dummy_td_phys;
    ed->tailp = dummy_td_phys;
    ed->next_ed = 0;


    return endpoint;
}

int ohci_controller::configure_endpoints(usb_device device, u16 bConfigurationValue, u32 maxpacketsize, u8 address, TimerSource &timer) {
    states = allocate_state(states, state_count, device);
    state_count++;


    u16 interfaces_count = states[state_count-1].interfaces_count;
    // we wanna use state_count-1 because state_count represents the highest object
    states[state_count-1].interfaces = new ohci_interface_state[interfaces_count];

    for (u16 i = 0; i < interfaces_count; i++) {
        states[state_count-1].interfaces[i].interface_number = device.interfaces[i].descriptor.bInterfaceNumber;
        states[state_count-1].interfaces[i].endpoint_count = device.interfaces[i].endpoint_count;
        states[state_count-1].interfaces[i].endpoints = new ohci_endpoint_state[device.interfaces[i].endpoint_count];
        for (u16 j = 0; j < states[state_count-1].interfaces[i].endpoint_count; j++) {
            states[state_count-1].interfaces[i].endpoints[j] = configure_endpoint(device, j, i, timer);
            if (states[state_count-1].interfaces[i].endpoints[j].dma.status != 0)
                return 1;
        }
    }

    return 0;
}

static constexpr u32 OHCI_BLE = 1u << 5;
static constexpr u32 OHCI_BLF = 1u << 2;

static bool stop_bulk_schedule(volatile ohci_registers_t *regs, volatile ohci_hcca *hcca, TimerSource &timer) {
    regs->control &= ~OHCI_BLE;

    (void)regs->control; // flush the register before waiting
    asm volatile("mfence" ::: "memory");

    u16 frame = hcca->frame_number;
    return wait_for_next_frame(hcca->frame_number, frame, timer);
}


static constexpr u32 OHCI_PLE = 1u << 2;

static bool stop_periodic_schedule(volatile ohci_registers_t *regs,
                                  volatile ohci_hcca *hc, TimerSource &timer) {
    regs->control &= ~OHCI_PLE;
    (void)regs->control;
    asm volatile("mfence" ::: "memory");
    u16 frame = hc->frame_number;
    return wait_for_next_frame(hc->frame_number, frame, timer);
}

bool ohci_controller::link_interrupt_ed(ohci_endpoint_state &ep, TimerSource &timer) {
    const unsigned period = usb_periodic::ohci_period(ep.interval);
    if (!period || !hcca) return false;
    auto *regs = (volatile ohci_registers_t *)controller.io_base;
    auto *hc = (volatile ohci_hcca *)hcca;
    const u32 saved = regs->control & OHCI_PLE;
    if (!stop_periodic_schedule(regs, hc, timer)) {
        controller.needs_reinit = true;
        return false;
    }
    ep.ed->next_ed = 0;
    if (!usb_periodic::install(hc->interrupt_table, 32, period, 0, ep.ed_phys)) {
        regs->control = (regs->control & ~OHCI_PLE) | saved;
        return false;
    }
    asm volatile("mfence" ::: "memory");
    ep.ed->control &= ~ohci::ed::skip;
    asm volatile("mfence" ::: "memory");
    ep.scheduled = true;
    regs->control |= OHCI_PLE;
    (void)regs->control;
    return true;
}

bool ohci_controller::unlink_interrupt_ed(ohci_endpoint_state &ep, TimerSource &timer) {
    auto *regs = (volatile ohci_registers_t *)controller.io_base;
    auto *hc = (volatile ohci_hcca *)hcca;
    ep.ed->control |= ohci::ed::skip;
    asm volatile("mfence" ::: "memory");
    if (!stop_periodic_schedule(regs, hc, timer) ||
        !usb_periodic::remove(hc->interrupt_table, 32, 0, ep.ed_phys)) {
        controller.needs_reinit = true;
        return false;
    }
    asm volatile("mfence" ::: "memory");
    ep.scheduled = false;
    return true; // Sole periodic request removed; leave PLE disabled.
}


bool ohci_controller::link_ed(ohci_endpoint_state &ep, TimerSource &timer) {
    if (controller.needs_reinit || !ep.ed ||
        (ep.transfer_type != 2 && ep.transfer_type != 3) || ep.scheduled)
        return false;

    if (ep.transfer_type == 3) return link_interrupt_ed(ep, timer);

    volatile ohci_registers_t *regs = (volatile ohci_registers_t *)controller.io_base;

    if (!stop_bulk_schedule(regs, hcca, timer)) {
        controller.needs_reinit = true;
        return false;
    }

    ep.ed->next_ed = regs->bulk_head_ed;
    regs->bulk_head_ed = ep.ed_phys;
    regs->bulk_current_ed = 0;

    ep.ed->control &= ~ohci::ed::skip;
    asm volatile("mfence" ::: "memory");

    ep.scheduled = true;
    regs->control |= OHCI_BLE;
    regs->command_status = OHCI_BLF;
    (void)regs->control;

    return true;
}


bool ohci_controller::unlink_ed(ohci_endpoint_state &ep, TimerSource &timer) {
    if (controller.needs_reinit || !ep.ed ||
        (ep.transfer_type != 2 && ep.transfer_type != 3))
        return false;

    if (!ep.scheduled)
        return true;

    if (ep.transfer_type == 3) return unlink_interrupt_ed(ep, timer);

    auto *regs =
        reinterpret_cast<volatile ohci_registers_t *>(controller.io_base);
    auto *hc = reinterpret_cast<volatile ohci_hcca *>(hcca);

    u32 saved_ble = regs->control & OHCI_BLE;

    ep.ed->control |= ohci::ed::skip;
    asm volatile("mfence" ::: "memory");

    if (!stop_bulk_schedule(regs, hc, timer)) {
        controller.needs_reinit = true;
        return false;
    }

    volatile ohci_ed *previous = nullptr;
    u32 current = regs->bulk_head_ed & 0xfffffff0u;
    bool removed = false;

    // Bound traversal so a corrupt/cyclic chain cannot hang the kernel.
    for (u32 visited = 0; current && visited < 4096; ++visited) {
        if (current == ep.ed_phys) {
            u32 next = ep.ed->next_ed;

            if (previous)
                previous->next_ed = next;
            else
                regs->bulk_head_ed = next;

            ep.ed->next_ed = 0;
            ep.scheduled = false;
            removed = true;
            break;
        }

        u64 address = dma_phys_to_virt(current);
        if (address == 0 || address == 1)
            break;

        previous = reinterpret_cast<volatile ohci_ed *>(address);
        current = previous->next_ed & 0xfffffff0u;
    }

    // Discard the cached traversal position while BLE is disabled.
    regs->bulk_current_ed = 0;
    asm volatile("mfence" ::: "memory");

    if (!removed) {
        controller.needs_reinit = true;
        return false; // Leave bulk processing disabled for recovery.
    }

    regs->control =
        (regs->control & ~OHCI_BLE) | saved_ble;

    if (saved_ble && regs->bulk_head_ed)
        regs->command_status = OHCI_BLF;

    (void)regs->control;
    return true;
}

usb_transfer_result ohci_controller::start_transfer(usb_async_request &async, u32 endpoint_address, void *buffer, u32 length, TimerSource &timer) {
    (void)timer;

    usb_transfer_result transfer = {0};

    transfer.controller_type = OHCI;
    transfer.controller_index = controller.index;

    transfer.endpoint_handle = endpoint_address;

    u8 port_index = (u8)((endpoint_address & 0xFF0000) >> 16);
    u8 interface_index = (u8)((endpoint_address & 0x00FF00) >> 8);
    u8 endpoint_index = (u8)(endpoint_address & 0x0000FF);

    if (controller.needs_reinit || (endpoint_address >> 24) || !ports ||
        port_index >= ports_count) {
        transfer.status_code = FAILED;
        return transfer;
    }

    if (!ports[port_index].connected) {
        printf("OHCI: error: invalid device port 0x%b not connected\n", ports[port_index].port_number);
        transfer.status_code = NOT_CONNECTED;
        return transfer; // return 1 for failure
    }

    if (!ports[port_index].device || !ports[port_index].device->discovered) {
        printf("OHCI: error: the device on port 0x%b was not discovered\n", ports[port_index].port_number);
        transfer.status_code = NOT_DISCOVERED;
        return transfer;
    }

    u8 device_address = ports[port_index].device->address;
    ohci_address_state *state = nullptr;
    for (u8 i = 0; i < state_count; i++) {
        if (states[i].device_address == device_address)
            state = &states[i];
    }

    if (!state) {
        printf("OHCI: error: no matching address_state found for device_address 0x%b\n", device_address);
        transfer.status_code = NO_ADDRESS;
        return transfer;
    }

    if (!state->interfaces || interface_index >= state->interfaces_count ||
        !state->interfaces[interface_index].endpoints ||
        endpoint_index >= state->interfaces[interface_index].endpoint_count) {
        transfer.status_code = FAILED;
        return transfer;
    }
    ohci_endpoint_state &ep = state->interfaces[interface_index].endpoints[endpoint_index];
    if (ep.scheduled || !ep.ed || !ep.dummy_td || !ep.data_td || !ep.buffer ||
        !ep.max_packet_size || (ep.ed->headp & OHCI_ED_HEAD_HALTED) ||
        (ep.ed->headp & OHCI_ED_HEAD_MASK) != ep.dummy_td_phys ||
        !(ep.endpoint_address & 0x0fu) ||
        (ep.low_speed && (ep.transfer_type != 3 || ep.max_packet_size > 8)) ||
        (!ep.low_speed && ep.max_packet_size > 64) ||
        (ep.transfer_type == 3 && !usb_periodic::ohci_period(ep.interval))) {
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

    if (ep.transfer_type != 2 && ep.transfer_type != 3) {
        transfer.status_code = FAILED;
        return transfer;
    }

    for (const auto &other : requests) {
        if (&other == &async || !other.active || !other.endpoint) continue;
        const auto &occupied = *(const ohci_endpoint_state *)other.endpoint;
        if (&occupied == &ep || (ep.transfer_type == 3 &&
            occupied.transfer_type == 3 && !other.completed)) {
            transfer.status_code = TRANSFER_BUSY;
            return transfer;
        }
    }

    const bool in = (ep.endpoint_address & 0x80u) != 0;

    ohci_td *data_td = ep.dummy_td;
    u32 data_td_phys = ep.dummy_td_phys;

    ohci_td *new_dummy = ep.data_td;
    u32 new_dummy_phys = ep.data_td_phys;


    if (!in && length)
        memcpy(ep.buffer, buffer, length);

    *new_dummy = {};
    *data_td = {};

    data_td->control =
        ohci::td::condition_not_accessed |
        (in ? ohci::td::in : ohci::td::out) |
        (in ? ohci::td::rounding : 0);

    data_td->cbp = length ? ep.buffer_phys : 0;
    data_td->be = length ? ep.buffer_phys + length - 1 : 0;
    data_td->next_td = new_dummy_phys;

    asm volatile("mfence" ::: "memory");
    ep.ed->tailp = new_dummy_phys;

    ep.dummy_td = new_dummy;
    ep.dummy_td_phys = new_dummy_phys;
    ep.data_td = data_td;
    ep.data_td_phys = data_td_phys;

    async.endpoint = &ep;
    async.dma_buffer = ep.buffer;
    async.in = in;
    transfer_phase(async, 10);
    transfer.status_code = TRANSFER_PENDING;
    return transfer;
}

void ohci_controller::advance_transfer(usb_async_request &async, TimerSource &timer) {
    auto &ep = *(ohci_endpoint_state *)async.endpoint;
    auto *regs = (volatile ohci_registers_t *)controller.io_base;
    auto *hc = (volatile ohci_hcca *)hcca;
    auto *ed = (volatile ohci_ed *)ep.ed;
    auto *td = (volatile ohci_td *)ep.data_td;
    const bool periodic = ep.transfer_type == 3;
    const u32 enable = periodic ? OHCI_PLE : OHCI_BLE;
    auto fail = [&](int code) {
        controller.needs_reinit = true;
        transfer_complete(async, code);
    };
    if (controller.needs_reinit) { fail(FAILED); return; }
    // The done queue is shared, so consume it even while stopping schedules.
    service_irq_events();
    if (controller.needs_reinit) { fail(FAILED); return; }
    switch (async.phase) {
    case 10:
        if (!acquire_schedule(async)) return;
        async.saved_command = regs->control;
        regs->control &= ~enable;
        (void)regs->control;
        asm volatile("mfence" ::: "memory");
        async.frame = hc->frame_number;
        transfer_phase(async, 1);
        return;
    case 1:
        if (hc->frame_number == async.frame) {
            if (phase_expired(async, timer, 100)) fail(LINK_ERROR);
            return;
        }
        if (periodic) {
            ep.ed->next_ed = 0;
            if (!usb_periodic::install(hc->interrupt_table, 32,
                    usb_periodic::ohci_period(ep.interval), 0, ep.ed_phys)) {
                fail(LINK_ERROR); return;
            }
        } else {
            ep.ed->next_ed = regs->bulk_head_ed;
            regs->bulk_head_ed = ep.ed_phys;
            regs->bulk_current_ed = 0;
        }
        asm volatile("mfence" ::: "memory");
        ep.ed->control &= ~ohci::ed::skip;
        asm volatile("mfence" ::: "memory");
        ep.scheduled = true;
        regs->control |= enable;
        if (!periodic) regs->command_status = OHCI_BLF;
        (void)regs->control;
        release_schedule(async);
        transfer_phase(async, 3);
        return;
    case 3:
        if (!(async.retired & OHCI_TD_DATA_DONE) &&
            !(ed->headp & OHCI_ED_HEAD_HALTED) &&
            !async.cancel && !transfer_expired(async, timer)) return;
        if (!acquire_schedule(async)) return;
        async.saved_command = regs->control;
        ep.ed->control |= ohci::ed::skip;
        asm volatile("mfence" ::: "memory");
        regs->control &= ~enable;
        (void)regs->control;
        async.frame = hc->frame_number;
        transfer_phase(async, 4);
        return;
    case 4:
        if (hc->frame_number == async.frame) {
            if (phase_expired(async, timer, 100)) fail(UNLINK_ERROR);
            return;
        }
        if (periodic) {
            if (!usb_periodic::remove(hc->interrupt_table, 32, 0, ep.ed_phys)) {
                fail(UNLINK_ERROR); return;
            }
        } else {
            volatile ohci_ed *previous = nullptr;
            u32 phys = regs->bulk_head_ed & 0xfffffff0u;
            bool found = false;
            for (u32 i = 0; phys && i < 4096; ++i) {
                if (phys == ep.ed_phys) {
                    if (previous) previous->next_ed = ep.ed->next_ed;
                    else regs->bulk_head_ed = ep.ed->next_ed;
                    found = true;
                    break;
                }
                u64 address = dma_phys_to_virt(phys);
                if (!address || address == 1) break;
                previous = (volatile ohci_ed *)address;
                phys = previous->next_ed & 0xfffffff0u;
            }
            if (!found) { fail(UNLINK_ERROR); return; }
            regs->bulk_current_ed = 0;
        }
        ep.scheduled = false;
        ep.ed->next_ed = 0;
        asm volatile("mfence" ::: "memory");
        transfer_phase(async, 5);
        return;
    case 5: {
        const u32 control = td->control;
        const u32 cc = (control >> 28) & 15u;
        async.result.hardware_status = control;
        if (cc < 14 && !(async.retired & OHCI_TD_DATA_DONE)) {
            if (phase_expired(async, timer, 20)) fail(FAILED);
            return;
        }
        // Hardware no longer references the retired/cancelled packet TD.
        ep.ed->headp = ep.dummy_td_phys | (ed->headp & 2u);
        asm volatile("mfence" ::: "memory");
        regs->control = (regs->control & ~enable) | (async.saved_command & enable);
        if (!periodic && (async.saved_command & enable) && regs->bulk_head_ed)
            regs->command_status = OHCI_BLF;
        (void)regs->control;
        if (cc >= 14) {
            transfer_complete(async, async.cancel ? TRANSFER_CANCELLED : TIMEOUT);
            return;
        }
        if (cc != 0) { fail(FAILED); return; }
        const u32 cbp = td->cbp;
        u32 actual = 0;
        if (!async.length) {
            if (cbp) { fail(INVALID_LENGTH); return; }
        } else if (!cbp) actual = async.length;
        else {
            const u64 base = ep.buffer_phys;
            if (cbp < base || (u64)cbp >= base + async.length) {
                fail(INVALID_LENGTH); return;
            }
            actual = (u32)((u64)cbp - base);
        }
        async.result.actual_length = actual;
        transfer_complete(async, TRANSFER_SUCCESS);
        return;
    }
    default: fail(FAILED); return;
    }
}


void ohci_controller::service_irq_events() {
    auto *regs = (volatile ohci_registers_t *)controller.io_base;
    const u32 status = regs->interrupt_status;
    if (status & (1u << 4)) {
        controller.needs_reinit = true;
        regs->interrupt_status = 1u << 4;
        return;
    }
    ohci_transfer_watch watch = {};
    watch.requests = requests;
    watch.request_count = request_capacity;
    if (!process_done_queue(hcca, regs, &watch)) controller.needs_reinit = true;
}
