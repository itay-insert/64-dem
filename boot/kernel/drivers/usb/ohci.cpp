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

typedef struct ohci_ed ohci_ed;
typedef struct ohci_td ohci_td;

struct alignas(16) ohci_ed {
    u32 control;
    u32 tailp;
    u32 headp;
    u32 next_ed;
};

struct alignas(16) ohci_td {
    u32 control;
    u32 cbp;       // Current Buffer Pointer
    u32 next_td;
    u32 be;        // Buffer End
};

static_assert(sizeof(ohci_ed) == 16);
static_assert(sizeof(ohci_td) == 16);


namespace ohci {
    namespace td {
        constexpr u32 condition_not_accessed = 0xFu << 28;
        constexpr u32 rounding               = 1u << 18;

        constexpr u32 setup = 0u << 19;
        constexpr u32 out   = 1u << 19;
        constexpr u32 in    = 2u << 19;

        constexpr u32 toggle_data0 = 1u << 24;
        constexpr u32 toggle_data1 = 2u << 24;

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
        "OHCI: TD 0x%x completed with CC=0x%x (%s), EC=%u\n",
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
    TimerSource& timer,
    u32& received_bytes
) {
    received_bytes = 0;

    for (u32 elapsed = 0; elapsed < 100; elapsed++) {
        
        // Process completed TDs whenever the HC writes back DoneHead
        if (regs->interrupt_status & OHCI_INTR_WDH)
            process_done_queue(hcca, regs);

        u32 headp = ed->headp;

        // The HC retired a TD with an error.
        if (headp & OHCI_ED_HEAD_HALTED)
            return ohci_transfer_result::transfer_error;

        // All three TDs have been retired successfully.
        if ((headp & OHCI_ED_HEAD_MASK) == dummy_phys) {

            u32 setup_cc = setup_td->control >> 28;
            u32 data_cc = data_td->control >> 28;
            u32 status_cc = status_td->control >> 28;

            if (setup_cc != 0 ||
                data_cc != 0 ||
                status_cc != 0) {
                return ohci_transfer_result::transfer_error;
            }

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
        u32 data_cc = (data_td->control >> 28) & 0xF;
        u32 status_cc = (status_td->control >> 28) & 0xF;

        // A TD with CC=NotAccessed was never retired.
        // Otherwise, it must have appeared in DoneHead.
        bool setup_reaped =
            setup_cc == OHCI_TD_CC_NOT_ACCESSED ||
            (watch->retired_mask & OHCI_TD_SETUP_DONE);

        bool data_reaped =
            data_cc == OHCI_TD_CC_NOT_ACCESSED ||
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
        &watch,
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
        controller.quarantined_dma = alloc;

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