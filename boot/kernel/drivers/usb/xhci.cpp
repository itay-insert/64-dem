#include <stdbool.h>
#include "uint_definitions.h"
#include "drivers/display/vga.h"
#include "x86-64/lowlevel.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/path.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_names.h"
#include "drivers/timers/timer.hpp"
#include "drivers/usb/xhci_events.h"
#include "drivers/usb/xhci.hpp"
#include "x86-64/memory/kmalloc.h"

#define TRB_CYCLE       (1u << 0)
#define TRB_ISP         (1u << 2)
#define TRB_CHAIN       (1u << 4)
#define TRB_IOC         (1u << 5)
#define TRB_IDT         (1u << 6)

#define TRB_TYPE(n)     ((u32)(n) << 10)

#define TRB_DIR_IN      (1u << 16)

#define TRB_SETUP       2
#define TRB_DATA        3
#define TRB_STATUS      4

#define TRT_NO_DATA     0
#define TRT_OUT         2
#define TRT_IN          3


xhci_controller::xhci_controller() {
    memcpy(vendor, "xHCI", sizeof(vendor));
    class_code = 0x0C;
    subclass = 0x03;
    prog_if = 0x30;
    type = MMIO;
    controller = {0};
    controller_type = xHCI;
    construct_controller();
}

static bool xhci_wait_clear(volatile u32 *reg, u32 mask) {
    u32 attempts = XHCI_POLL_LIMIT;

    while (attempts--) {
        if ((*reg & mask) == 0)
            return true;

        __asm__ __volatile__("pause");
    }

    return false;
}



static bool xhci_wait_set(volatile u32 *reg, u32 mask)  {
    u32 attempts = XHCI_POLL_LIMIT;

    while (attempts--) {
        if ((*reg & mask) == mask)
            return true;

        __asm__ __volatile__("pause");
    }

    return false;
}


int xhci_controller::xhci_reset() {
    volatile xhci_cap_regs *xhci_cap;
    xhci_cap = (volatile xhci_cap_regs *)xhci_base;

    uint16_t version = xhci_cap->hciversion;

    printf("xHCI version: %w\n", version);

    uint32_t hcs1 = xhci_cap->hcsparams1;

    uint8_t max_slots = hcs1 & 0xff;
    uint8_t max_ports = (hcs1 >> 24) & 0xff;

    printf("Slots: %d\n", (int)max_slots);
    printf("Ports: %d\n", (int)max_ports);

    volatile xhci_op_regs *op = (volatile xhci_op_regs *)
          (xhci_base + xhci_cap->caplength);

    if (!xhci_wait_clear(&op->usbsts, XHCI_STS_CNR)) {
        printf("xHCI: controller stayed not ready\n");
        return 1;
    }

    op->usbcmd &= ~1;

    if (!xhci_wait_set(&op->usbsts, XHCI_STS_HCH)) {
        printf("xHCI: controller failed to halt\n");
        return 1;
    }

    op->usbcmd |= (1 << 1);

    if (!xhci_wait_clear(&op->usbcmd, XHCI_CMD_HCRST)) {
        printf("xHCI: controller reset timed out\n");
        return 1;
    }

    if (!xhci_wait_clear(&op->usbsts, XHCI_STS_CNR)) {
        printf("xHCI: controller stayed not ready after reset\n");
        return 1;
    }

    printf("xHCI: controller halted and reset\n");

    return 0;
}


int xhci_controller::allocate_scracthpad(u32 scratchpad_count, bool addr_64) {
    dma_ret scratchpad = allocate_dma((u64)scratchpad_count * sizeof(u64));

    u64 bytes = scratchpad.SizeInPages * 4096;
    u64 end = scratchpad.physical_address + bytes;

    if (scratchpad.status != 0 || (!addr_64 && (end < scratchpad.physical_address || end > 0x100000000ULL)))
        return 1;


    scratch_pad = (u64 *)scratchpad.virtual_address;
    scratchpad_physical = scratchpad.physical_address;

    memset(scratch_pad, 0, (size_t)(scratchpad_count * sizeof(u64)));

    for (u32 i = 0; i < scratchpad_count; i++) {
        dma_ret buffer = allocate_dma(4096);

        bytes = buffer.SizeInPages * 4096;
        end = buffer.physical_address + bytes;

        if (buffer.status != 0 || (!addr_64 && (end < buffer.physical_address || end > 0x100000000ULL)))
         return 1;

        memset((void *)buffer.virtual_address, 0, 4096);

        scratch_pad[i] = buffer.physical_address;
    }

    return 0;
}

volatile u32 *xhci_controller::xhci_find_extended_capability(volatile xhci_cap_regs *cap, 
u8 wanted_id) {
    u32 offset = (cap->hccparams1 >> 16) & 0xFFFF;

    while (offset != 0) {
        volatile u32 *entry = 
            (volatile u32 *)(xhci_base + (u64)(offset << 2));

        u32 header = entry[0];
        u8 id = header & 0xff;
        u8 next = (header >> 8) & 0xff;

        if (id == wanted_id)
            return entry;

        if (next == 0)
            break;

        offset += next;

    }
    return NULL;
}


bool xhci_controller::xhci_take_ownership(volatile xhci_cap_regs *cap, TimerSource& timer) {
    volatile u32 *legacy = xhci_find_extended_capability(cap, XHCI_EXT_CAP_LEGACY);

    if (legacy == NULL) {
        printf("xHCI: no legacy ownership capabillity\n");
        return true;
    }

    const u64 frequency = timer.read_freq();
    if (frequency == 0) {
        printf("xHCI: handoff timer has zero frequency\n");
        return false;
    }

    printf("xHCI: handoff before USBLEGSUP=0x%ux USBLEGCTLSTS=0x%ux\n",
           legacy[0], legacy[1]);

    // The PM timer may be 24-bit. Accumulate short deltas so wraparound is safe.
    u32 previous_tick = (u32)timer.read() & 0x00FFFFFFu;
    u64 elapsed_ticks = 0;

    // Write only the OS semaphore byte; firmware may change the BIOS byte at any time.
    volatile u8 *os_semaphore = (volatile u8 *)legacy + 3;
    *os_semaphore = (u8)(*os_semaphore | 1u);

    u32 support = legacy[0];
    printf("xHCI: handoff request USBLEGSUP=0x%ux BIOS=%u OS=%u\n",
           support, (support & XHCI_LEGACY_BIOS_OWNED) != 0,
           (support & XHCI_LEGACY_OS_OWNED) != 0);

    u32 last_semaphores = support &
        (XHCI_LEGACY_BIOS_OWNED | XHCI_LEGACY_OS_OWNED);

    while (elapsed_ticks < frequency) { // one second
        support = legacy[0];
        u32 semaphores = support &
            (XHCI_LEGACY_BIOS_OWNED | XHCI_LEGACY_OS_OWNED);
        u32 tick = (u32)timer.read() & 0x00FFFFFFu;
        elapsed_ticks += (tick - previous_tick) & 0x00FFFFFFu;
        previous_tick = tick;

        if (semaphores != last_semaphores) {
            printf("xHCI: handoff at %lu ms USBLEGSUP=0x%ux BIOS=%u OS=%u\n",
                   (elapsed_ticks * 1000) / frequency, support,
                   (support & XHCI_LEGACY_BIOS_OWNED) != 0,
                   (support & XHCI_LEGACY_OS_OWNED) != 0);
            last_semaphores = semaphores;
        }

        if (semaphores == XHCI_LEGACY_OS_OWNED)
            break;

        __asm__ __volatile__("pause");
    }

    if ((support & (XHCI_LEGACY_BIOS_OWNED | XHCI_LEGACY_OS_OWNED)) !=
        XHCI_LEGACY_OS_OWNED) {
        printf("xHCI: handoff timeout after %lu ms USBLEGSUP=0x%ux USBLEGCTLSTS=0x%ux BIOS=%u OS=%u\n",
               (elapsed_ticks * 1000) / frequency, support, legacy[1],
               (support & XHCI_LEGACY_BIOS_OWNED) != 0,
               (support & XHCI_LEGACY_OS_OWNED) != 0);
        return false;
    }

    // Preserve the low reserved bits while writing zero to the upper status bits.
    // The upper status bits include write-one-to-clear flags.
    u32 ctlsts = legacy[1];
    legacy[1] = ctlsts & 0x0000FFFFu & ~XHCI_LEGACY_SMI_ENABLES;
    printf("xHCI: ownership acquired in %lu ms USBLEGSUP=0x%ux USBLEGCTLSTS=0x%ux\n",
           (elapsed_ticks * 1000) / frequency, support, legacy[1]);
    return true;
}



static volatile u32 *mmio32(u64 address) {
    return reinterpret_cast<volatile u32 *>(address);
}



static void write_pointer_register(u64 address, u64 value, bool ac64) {
    if (ac64)
        *reinterpret_cast<volatile u64 *>(address) = value;
    else
        *mmio32(address) = static_cast<u32>(value);
}



static bool wait_register(TimerSource& timer, volatile u32 *reg,
                            u32 mask, bool want_set, u32 timeout_ms) {
    const u64 frequency = timer.read_freq();
    if (frequency == 0) return false;

    const u64 limit = (frequency * timeout_ms + 999) / 1000;
    u32 previous = static_cast<u32>(timer.read()) & 0x00ffffffu;
    u64 elapsed = 0;

    while (elapsed < limit) {
        const u32 value = *reg;
        if (want_set ? ((value & mask) == mask) : ((value & mask) == 0))
            return true;

        const u32 now = static_cast<u32>(timer.read()) & 0x00ffffffu;
        elapsed += (now - previous) & 0x00ffffffu;
        previous = now;
        asm volatile("pause");
    }
    return false;
}

static bool xhci_dma_alloc(dma_ret *out, u64 bytes, bool ac64) {
    *out = allocate_dma(bytes);
    if (out->status != 0) return false;

      // AC64=0 means every DMA pointer supplied to the controller must fit below 4 GiB.
    if (!ac64 &&
        (out->physical_address >= 0x100000000ULL ||
        bytes > 0x100000000ULL - out->physical_address)) {
        free_dma(out->virtual_address);
        *out = {};
        return false;
    }

    memset(reinterpret_cast<void *>(out->virtual_address), 0, bytes);
    return true;
}


bool xhci_controller::setup_rings(volatile xhci_cap_regs *cap,
                          volatile xhci_op_regs *op, bool ac64) {
    constexpr u32 TRBS = 256;
    rings.ac64 = ac64;

    if (!xhci_dma_alloc(&rings.command_dma, TRBS * sizeof(XhciTrb), ac64) ||
          !xhci_dma_alloc(&rings.event_dma,   TRBS * sizeof(XhciTrb), ac64) ||
          !xhci_dma_alloc(&rings.erst_dma, sizeof(XhciErstEntry), ac64))
        return false;

    rings.command =
          reinterpret_cast<XhciTrb *>(rings.command_dma.virtual_address);
    rings.event =
          reinterpret_cast<XhciTrb *>(rings.event_dma.virtual_address);

    rings.doorbell_base = xhci_base + (cap->dboff & ~3u);
    const u64 runtime_base = xhci_base + (cap->rtsoff & ~31u);
    rings.interrupter0_base = runtime_base + 0x20;

      // Last command-ring entry links back to entry 0.
    XhciTrb& link = rings.command[TRBS - 1];
    link.d0 = static_cast<u32>(rings.command_dma.physical_address);
    link.d1 = static_cast<u32>(rings.command_dma.physical_address >> 32);
    link.d2 = 0;
    link.d3 = (6u << 10) | (1u << 1) | 1u; // Link type, Toggle Cycle, cycle=1

      // CRCR.RCS=1 establishes the command ring's initial consumer cycle.
    write_pointer_register(reinterpret_cast<u64>(&op->crcr),
                             rings.command_dma.physical_address | 1u, ac64);

    auto *erst =
          reinterpret_cast<XhciErstEntry *>(rings.erst_dma.virtual_address);
    erst[0].segment_base = rings.event_dma.physical_address;
    erst[0].segment_size_trbs = TRBS;
    erst[0].reserved = 0;

    *mmio32(rings.interrupter0_base + 0x08) = 1; // ERSTSZ: one segment
    write_pointer_register(rings.interrupter0_base + 0x18,
                             rings.event_dma.physical_address, ac64); // ERDP
    write_pointer_register(rings.interrupter0_base + 0x10,
                             rings.erst_dma.physical_address, ac64);  // ERSTBA
    rings.command_index = 0;
    rings.command_cycle = 1;
    rings.event_index = 0;
    rings.event_cycle = 1;
    __atomic_store_n(&pending_command_done, 0u, __ATOMIC_RELEASE);
    return true;
}

u64 xhci_controller::submit_command(XhciTrb command) {
    const u32 index = rings.command_index;
    const u64 physical =
          rings.command_dma.physical_address + index * sizeof(XhciTrb);

    XhciTrb& entry = rings.command[index];
    entry.d0 = command.d0;
    entry.d1 = command.d1;
    entry.d2 = command.d2;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    entry.d3 = (command.d3 & ~1u) | rings.command_cycle;

    pending_command_physical = physical;
    __atomic_store_n(&pending_command_done, 0u, __ATOMIC_RELEASE);

    if (++rings.command_index == 255) {
          // Publish the Link TRB for this traversal, then wrap.
        rings.command[255].d3 =
            (6u << 10) | (1u << 1) | rings.command_cycle;
        rings.command_index = 0;
        rings.command_cycle ^= 1;
    }

    __atomic_thread_fence(__ATOMIC_RELEASE);
    *mmio32(rings.doorbell_base) = 0; // Doorbell 0, command target
    return physical;
}



bool xhci_controller::poll_event(XhciTrb *out) {
    volatile XhciTrb *entry =
          reinterpret_cast<volatile XhciTrb *>(rings.event)
          + rings.event_index;

    if ((entry->d3 & 1u) != rings.event_cycle)
        return false;

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    out->d0 = entry->d0;
    out->d1 = entry->d1;
    out->d2 = entry->d2;
    out->d3 = entry->d3;

    if (++rings.event_index == 256) {
        rings.event_index = 0;
        rings.event_cycle ^= 1;
    }

    return true;
}

extern "C" u32 xhci_controller::xhci_process_events(void) {
    if (rings.event == nullptr)
        return 0;
    if (__atomic_exchange_n(&processing_events, 1u, __ATOMIC_ACQUIRE))
        return 0; // Another caller owns the single-consumer event ring.

    // Acknowledge the interrupt while EHB still blocks a new one. Clearing
    // IP after EHB could erase an interrupt raised by a newly arrived event.
    auto *op = reinterpret_cast<volatile xhci_op_regs *>(
        xhci_base + reinterpret_cast<volatile xhci_cap_regs *>(xhci_base)->caplength);
    if (op->usbsts & (1u << 3))
        op->usbsts = 1u << 3; // USBSTS.EINT is RW1C; clear it before IMAN.IP.
    volatile u32 *iman = mmio32(rings.interrupter0_base);
    const u32 pending = *iman;
    if (pending & 1u)
        *iman = (pending & 2u) | 1u; // Preserve IE, clear IP.

    u32 processed = 0;
    XhciTrb event;
    while (processed < 256 && poll_event(&event)) {
        const u32 type = (event.d3 >> 10) & 0x3fu;

        if (type == 33) { // Command Completion Event
            const u64 command_physical =
                (static_cast<u64>(event.d1) << 32) | event.d0;
            if (command_physical == pending_command_physical) {
                pending_command_completion = event;
                __atomic_store_n(&pending_command_done, 1u, __ATOMIC_RELEASE);
            }
        } else if (type == 34) { // Port Status Change Event
            const u32 port = event.d0 >> 24;
            if (port != 0 && port < 256) {
                __atomic_fetch_or(&pending_port_changes[port >> 5],
                                  1u << (port & 31), __ATOMIC_RELEASE);
            }
        } else if (type == 32) { 
            u8 slot_id = (event.d3 >> 24) & 0xFF;
            u8 endpoint_id = (event.d3 >> 16) & 0x1F;

            bool event_data = (event.d3 & (1u << 2)) != 0;

            u64 trb_pointer =
                ((u64)event.d1 << 32) | event.d0;
            const u8 completion_code = event.d2 >> 24;

            if (__atomic_load_n(&transfer_waiting, __ATOMIC_ACQUIRE) &&
                !__atomic_load_n(&transfer_completed, __ATOMIC_ACQUIRE) &&
                !event_data &&
                    slot_id == transfer_slot &&
                    endpoint_id == transfer_endpoint &&
                    (trb_pointer == transfer_trb_pointer ||
                     (completion_code != 1 && completion_code != 13))) {

                transfer_completion = event;
                __atomic_store_n(&transfer_completed, true, __ATOMIC_RELEASE);
            } else {
            // Handle or queue unrelated Transfer Events.
            }
        } else if (type == 37) { // Host Controller Event
            // Add controller error reporting when recovery is implemented.
        }
        ++processed;
    }

    // ERDP points to the next unread TRB. Clear EHB last, allowing a newly
    // queued event to trigger another interrupt after this drain.
    if (processed != 0 || (pending & 1u) ||
        (*mmio32(rings.interrupter0_base + 0x18) & (1u << 3))) {
        const u64 next = rings.event_dma.physical_address
                       + rings.event_index * sizeof(XhciTrb);
        write_pointer_register(rings.interrupter0_base + 0x18,
                               next | (1u << 3), rings.ac64);
    }

    __atomic_store_n(&processing_events, 0u, __ATOMIC_RELEASE);
    return processed;
}

extern "C" int xhci_controller::xhci_take_port_change(u8 port_number) {
    if (port_number == 0)
        return 0;
    const u32 mask = 1u << (port_number & 31);
    return (__atomic_fetch_and(&pending_port_changes[port_number >> 5],
                               ~mask, __ATOMIC_ACQ_REL) & mask) != 0;
}

extern "C" void xhci_controller::xhci_set_interrupt_driven(int enabled) {
    __atomic_store_n(&interrupt_driven, enabled != 0, __ATOMIC_RELEASE);
}



bool xhci_controller::wait_command(TimerSource& timer, u64 command_physical,
                           XhciTrb *completion, u32 timeout_ms) {
    const u64 frequency = timer.read_freq();
    if (frequency == 0) return false;

    const u64 limit = (frequency * timeout_ms + 999) / 1000;
    u32 previous = static_cast<u32>(timer.read()) & 0x00ffffffu;
    u64 elapsed = 0;

    while (elapsed < limit) {
        // Keep polling as a fallback if this machine has not routed xHCI IRQs.
        xhci_process_events();

        if (__atomic_load_n(&pending_command_done, __ATOMIC_ACQUIRE) &&
            pending_command_physical == command_physical) {
            *completion = pending_command_completion;
            __atomic_store_n(&pending_command_done, 0u, __ATOMIC_RELEASE);
            return true;
        }

        const u32 now = static_cast<u32>(timer.read()) & 0x00ffffffu;
        elapsed += (now - previous) & 0x00ffffffu;
        previous = now;
        asm volatile("pause");
    }
    return false;
}


static inline u32 read_portsc(u64 base, u8 reg) {
    return MMIO_read32(base, (reg * 0x10));
}

int xhci_controller::controller_init(TimerSource &timer) {
    u64 page_offset = controller.bar.address & 0xfffULL;
    u64 pages = (page_offset + controller.bar.size + 0xfffULL) >> 12;

    printf("xHCI MMIO physical address: 0x%lx\n", controller.bar.address);

    u64 physical = controller.bar.address & ~0xfffULL;
    u64 virtual_address = BASE + physical;

    controller.io_base = virtual_address + page_offset;

   
    xhci_base = controller.io_base; // pass controller.io_base to xhci_base for compatibillity issues
    volatile xhci_cap_regs *cap;
    cap = (volatile xhci_cap_regs *)controller.io_base;

    if (!xhci_take_ownership(cap, timer))
        return 1;

    if (xhci_reset() != 0) {
        printf("xHCI: failed to reset\n");
        return 1;
    }

    volatile xhci_op_regs *op = (volatile xhci_op_regs *)
        (xhci_base + cap->caplength);

    u32 hcs1 = cap->hcsparams1;
    u32 hcs2 = cap->hcsparams2;
    u32 hcc1 = cap->hccparams1;

    u8 max_slots = hcs1 & 0xff;
    u16 max_intrs = (hcs1 >> 8) & 0x7ff;
    u8 max_ports = (hcs1 >> 24) & 0xff;
    bool context_64 = (hcc1 & (1u << 2)) != 0; // CSZ
    bool addr_64 = (hcc1 & 1u) != 0; // AC64

    u32 dboff = cap->dboff;

    u32 doorbell_offset = dboff & ~0x3u;

    doorbells = (volatile u32 *)((u8 *)controller.io_base + doorbell_offset);

    if (!(op->pagesize & 1)) {
        printf("xHCI: 4 KiB pages unsupported\n");
        return 1;
    }

    u32 hi = (hcs2 >> 21) & 0x1f;
    u32 lo = (hcs2 >> 27) & 0x1f;
    u32 scratchpad_count = (hi << 5) | lo;

    if (scratchpad_count > 0) {
        int alsts = allocate_scracthpad(scratchpad_count, addr_64);

        if (alsts != 0) {
            printf("xHCI: failed to allocate scracthpad\n");
            return 1;
        }

        printf("xHCI: allocated scracthpad\n");

    }

    if (max_slots == 0 || max_ports == 0) {
        printf("xHCI: controller advertises no slots or ports\n");
        return 1;
    }
    u8 enabled_slots = max_slots;
    u64 dcbaa_size = ((u64)enabled_slots + 1) * sizeof(u64);

    dma_ret dcbaa_array = allocate_dma(dcbaa_size);

    u64 bytes = dcbaa_array.SizeInPages * 4096;
    u64 end = dcbaa_array.physical_address + bytes;

    if (dcbaa_array.status != 0 || (!addr_64 && (end < dcbaa_array.physical_address || end > 0x100000000ULL))) {
        printf("xHCI: failed to allocate dcbaa\n");
        return 1;
    }

    dcbaa = (u64 *)dcbaa_array.virtual_address;

    memset(dcbaa, 0, dcbaa_size);
    
    if (scratchpad_count > 0)
        dcbaa[0] = scratchpad_physical;
    else
        dcbaa[0] = 0;


    printf("xHCI: allocated dcbaa\n");

    op->dcbaap = dcbaa_array.physical_address;

    printf("xHCI: programmed the DCBAAP\n");

    op->config = (op->config & ~0xffu) | enabled_slots;

    u32 hccparams1 = cap->hccparams1;
    u32 xecp = (hccparams1 >> 16) & 0xFFFF;

    volatile u32 *ext_cap =
        (volatile u32 *)(xhci_base + ((u64)xecp << 2));

    if (!setup_rings(cap, op, addr_64)) {
      printf("xHCI: ring allocation/setup failed\n");
      return 1;
    }

    op->usbcmd |= 1u; // USBCMD.Run/Stop

    if (!wait_register(timer, &op->usbsts, XHCI_STS_HCH, false, 1000)) {
      printf("xHCI: did not enter running state, USBSTS=0x%ux\n", op->usbsts);
      return 1;
    }

    XhciTrb no_op = {};
    no_op.d3 = 23u << 10; // No Op Command TRB
    const u64 no_op_physical = submit_command(no_op);

    XhciTrb completion;
    if (!wait_command(timer, no_op_physical, &completion, 1000)) {
      printf("xHCI: No Op command timed out\n");
      return 1;
    }

    const u32 completion_code = completion.d2 >> 24;
    if (completion_code != 1) { // 1 = Success
      printf("xHCI: No Op failed, completion code=%u\n", completion_code);
      return 1;
    }

    printf("xHCI: running; command and event rings work\n");


    
    return 0;
}

static inline u32 read_port(u64 op_base, u8 port) {
    volatile u32 *PortSC = (volatile u32 *)(op_base + XHCI_PORTSC_BASE + ((u64)port * XHCI_PORTSC_STRIDE));
    return (u32)*PortSC;
}

int xhci_controller::enumrate_ports() {
    volatile xhci_cap_regs *cap_regs = (volatile xhci_cap_regs *)controller.io_base;

    u64 op_base = controller.io_base + (u64)cap_regs->caplength; // calculate op_base from io_base + cap_length
    u8 xhci_port_count = (u8)((cap_regs->hcsparams1 >> 24) & 0xFF);

    ports = new usb_port_descriptor[xhci_port_count]; // allocate the port descriptors
    slots = new usb_slot[xhci_port_count];

    if (ports == nullptr || slots == nullptr) // return -1 for allocation failure
        return -1;

    memset(slots, 0, sizeof(usb_slot) * xhci_port_count);


    for (u8 port = 0; port < xhci_port_count; port++) {
        u32 status = read_port(op_base, port);
        ports[port].port_number = port + 1;
        ports[port].port_status = status;
        ports[port].connected = (status & CCS);
        ports[port].controller_type = xHCI;
    }

    return (int)xhci_port_count;
}

void xhci_controller::scan_ports(TimerSource &timer) {
    // HCRST puts root ports into Disconnected. A device present at boot may
    // still be training its link when the first PORTSC snapshot is taken.
    wait_ms(timer, 100);
    auto *cap = (volatile xhci_cap_regs *)controller.io_base;
    const u64 op_base = controller.io_base + cap->caplength;
    u8 *attempted = new u8[ports_count]{};
    if (!attempted)
        return;

    for (u32 elapsed = 0; elapsed <= 500; elapsed += 10) {
        for (int i = 0; i < ports_count; ++i) {
            if (attempted[i])
                continue;
            const u32 status = read_port(op_base, (u8)i);
            ports[i].port_status = status;
            ports[i].connected = (status & CCS) != 0;
            if (status == 0xffffffffu) {
                printf("xHCI: port %u has unreadable PORTSC\n", (u32)(i + 1));
                ports[i].connected = false;
                attempted[i] = 1;
                continue;
            }
            if (!ports[i].connected)
                continue;

            attempted[i] = 1;
            printf("xHCI: port %u connected, PORTSC=0x%ux, speed ID=%u\n",
                   (u32)(i + 1), status, (status >> 10) & 0x0fu);
            usb_device enumerated = get_device(ports[i].port_number, timer);
            if (enumerated.discovered)
                ports[i].device = new usb_device(enumerated);
            else
                printf("xHCI: device enumeration failed on port %u, PORTSC=0x%ux\n",
                       (u32)(i + 1), read_port(op_base, (u8)i));
        }
        if (elapsed != 500)
            wait_ms(timer, 10);
    }
    u32 connected_count = 0;
    for (int i = 0; i < ports_count; ++i)
        if (ports[i].connected)
            ++connected_count;
    if (!connected_count)
        printf("xHCI: no root-port devices detected after boot scan (%d ports)\n",
               ports_count);
    delete[] attempted;
}


static inline u32 xhci_portsc_neutral(u32 value) {
    return (value & RO_MASK) | (value & RWS_MASK);
}

int xhci_controller::reset_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;

    volatile u8 *caplength = (volatile u8 *)controller.io_base;
    
    u64 op_base = controller.io_base + (u64)*caplength;
    volatile u32 *portsc = (volatile u32 *)(op_base + XHCI_PORTSC_BASE + ((port - 1) * XHCI_PORTSC_STRIDE));

    u32 status = *portsc;

    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (!(status & CCS))
        return NO_DEVICE;

    if (status & OCA)
        return OVER_CURRENT_CONDITION;  // over-current condition

    if (status & PR)
        return RESET_STILL_ASSERTED;

    // SuperSpeed attach enables the port automatically. Resetting an already
    // enabled port can disturb the link and is unnecessary for enumeration.
    u8 protocol_major = 0;
    read_protocol_slot_type(port, &protocol_major);
    if (protocol_major >= 3 && (status & PED) &&
        (status & PLS_MASK) == U0)
        return RESET_SUCCESS;

    
    // Clear previous reset-complete flag so that we
    // can detect completion of this reset
    *portsc = xhci_portsc_neutral(status) | PRC;


    status = *portsc;

    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    if (!(status & CCS))
        return NO_DEVICE;

    if (status & PRC)
        return RESET_FAILED; // old flag did not clear

    // intiate reset
    *portsc = xhci_portsc_neutral(status) | PR;


    // wait for hardware to finish the reset
    for (u32 elapsed = 0; elapsed < RESET_TIMEOUT; elapsed++) {
        status = *portsc;

        if (status == 0xFFFFFFFF)
            return INVALID_STATUS;

        if (!(status & CCS)) 
            return NO_DEVICE;

        if (status & OCA)
            return OVER_CURRENT_CONDITION;

        // PRC is set when reset processing completes
        if (status & PRC) {
            if (status & PR)
                return RESET_STILL_ASSERTED;

            // we need successfull reset, not merely completion
            if (!(status & PED))
                return RESET_FAILED;
                
            if ((status & PLS_MASK) != U0)
                return RESET_FAILED;

            return RESET_SUCCESS;
        }

        wait_ms(timer, 1);
    }

    status = *portsc;

    if ((status != 0xFFFFFFFF) && (status & PR))
        return RESET_STILL_ASSERTED;

    return RESET_FAILED;
}


u64 xhci_controller::port_to_address(u8 port) {
    volatile xhci_cap_regs *cap_regs = (volatile xhci_cap_regs *)controller.io_base;

    u64 op_base = controller.io_base + (u64)cap_regs->caplength;
    u64 port_base = op_base + XHCI_PORTSC_BASE + ((port - 1) * XHCI_PORTSC_STRIDE);
    return port_base;
}


int xhci_controller::disable_port(u8 port, TimerSource &timer) {
    if (port < 1 || port > ports_count)
        return INVALID_PORT;

    volatile u32 *portsc = (volatile u32 *)port_to_address(port);

    u32 status = *portsc;
    if (status == 0xFFFFFFFF)
        return INVALID_STATUS;

    status &= ~PR;   // don't request reset simultaneously
    status |= ~PED; // writing 1 to the PED bit to disable the port
    *portsc = status;
    (void)*portsc; // register read to flush MMIO write

    for (int i = 0; i < 100; i++) {  // poll with a 100 ms timeout
        if ((*portsc & PED) == 0)
            break;
        wait_ms(timer, 1); // wait 1 milesecond
    }

    return (*portsc & PED) ? RESET_FAILED : RESET_SUCCESS;
}


u8 xhci_controller::read_protocol_slot_type(u8 port, u8 *major) {
    // define pointers
    volatile xhci_cap_regs *cap_regs = (volatile xhci_cap_regs *)controller.io_base;
    volatile u32 *cap = (volatile u32 *)controller.io_base;
    
    u32 hccparams1 = cap_regs->hccparams1; // get hccparams1 from cap_regs


    u32 xecap = (hccparams1 >> 16) & 0xFFFF; // extract xecap from hccparams1

    u32 offset = xecap * 4; // calculate the offset from xecap

    
    for (u32 remaining = 256; offset != 0 && remaining != 0; --remaining) {
        u32 header = cap[offset / 4];

        u8 cap_id = header & 0xFF;
        u8 next = (header >> 8) & 0xFF;

        if (cap_id == 2) {
            u32 port_info = cap[(offset + 0x08) / 4];
            u32 slot_info = cap[(offset + 0x0C) / 4];

            u8 port_offset = port_info & 0xFF;
            u8 port_count = (port_info >> 8) & 0xFF;

            u8 slot_type = slot_info & 0x1F;

            if (port >= port_offset && port < (u32)(port_offset+port_count)) {
                if (major)
                    *major = (header >> 24) & 0xff;
                return slot_type;
            }
        }

        if (next == 0)
            break;
        
        offset += (u32)next * 4;
    }

    printf("xHCI: failed to find a valid slot_type for port 0x%b\n", port);
    return 0xFF; // 0xFF for invalid slot_type
} 

#define XHCI_ENABLE_SLOT (9u << 10)
#define XHCI_ENABLE_TIMEOUT 500


u8 xhci_controller::enable_device_slot(u8 port, TimerSource &timer) {

    // Get the protocol slot type
    u8 slot_type = read_protocol_slot_type(port);

    if (slot_type == 0xFF)
        return 0; // if the slot_type is invalid (slot_type == 0xFF), return invalid slot_id (slot_id == 0)


    XhciTrb command = {}; // initialize the XhciTrb enable slot command

    command.d3 = XHCI_ENABLE_SLOT | ((u32)slot_type << 16);


    // submit command
    u64 command_phys = submit_command(command);


    // wait for completiom
    XhciTrb completion = {};

    if (!wait_command(timer, command_phys, &completion, 500)) {
        printf("xHCI: Enable Slot for port 0x%b Timed out!\n", port);
        return 0;
    }

    u8 completion_code = completion.d2 >> 24;

    if (completion_code != 1) {
        printf("xHCI: Enable slot failed, code=0x%b\n", completion_code);
        return 0;
    }

    u8 slot_id = completion.d3 >> 24;

    if (slot_id == 0) {
        printf("xHCI: invalid slot_id for port 0x%b\n", port);
        return 0;
    }

    printf("xHCI: enabled slot 0x%b for port 0x%b\n", slot_id, port);
    return slot_id;
}


bool xhci_controller::disable_device_slot(u8 slot_id, TimerSource &timer) {
    XhciTrb disable_slot = {};

    disable_slot.d0 = 0;
    disable_slot.d1 = 0;
    disable_slot.d2 = 0;
    disable_slot.d3 =
        (10u << 10) | // TRB Type = disable slot
        ((u32)slot_id << 24); // slot id

    // submit command
    u64 command_phys = submit_command(disable_slot);

    XhciTrb completion = {};
    if (!wait_command(timer, command_phys, &completion, 500)) {
        printf("xHCI: disable_Slot Timed out, slot_id=0x%b\n", slot_id);
        return false;
    }

    u8 completion_code = completion.d2 >> 24;
    if (completion_code != 1) {
        printf("xHCI: disable_Slot failed, slot_id=0x%b, completion_code=0x%b\n", slot_id, completion_code);
        return false;
    }

    return true;
}

#define CSZ (1u << 2)
#define EP0_SIZE (sizeof(XhciTrb) << 8) // sizeof(trb) * 256
#define EP0_LINK 255


static bool get_initial_mps(u64 op, u8 port, u8 protocol_major,
                            u16 &initial_mps) {
    u64 portsc_offset = 0x400 + ((port - 1) * XHCI_PORTSC_STRIDE);

    volatile u32 *port_reg = (volatile u32 *)((u8 *)op + portsc_offset);

    u32 status = *port_reg;

    u8 speed_id = (status >> 10) & 0xF;

    initial_mps = 0;

    // The port's speed ID is a protocol-defined PSI value. USB3 controllers
    // can assign IDs other than 4 to SuperSpeed or SuperSpeedPlus links.
    if (protocol_major >= 3 && speed_id != 0) {
        initial_mps = 512;
        return true;
    }

    switch (speed_id) {
        case 1: // full speed
        case 2: // low speed
            initial_mps = 8;
            break;

        case 3: // high speed
            initial_mps = 64;
            break;

        case 4: // Default SuperSpeed mapping if no protocol cap is present.
            if (protocol_major == 0) {
                initial_mps = 512;
                break;
            }
            return false;

        default:
            printf("xHCI: unsupported speed_id on port 0x%b, speed_id=0x%b\n", port, speed_id);
            return false;
        
    }

    return true;
}


static inline u8 get_port_speed_id(u64 op, u8 port) {
    u64 portsc_offset = 0x400 + ((port - 1) * XHCI_PORTSC_STRIDE);

    volatile u32 *port_reg = (volatile u32 *)((u8 *)op + portsc_offset);

    return (u8)((*port_reg >> 10) & 0xF);
} 

bool xhci_controller::wait_transfer_event(
    TimerSource &timer,
    u8 slot_id,
    u8 endpoint_id,
    u64 expected_trb,
    XhciTrb *result,
    u32 timeout)
{
    if (__atomic_load_n(&transfer_waiting, __ATOMIC_ACQUIRE)) {
        if (transfer_slot != slot_id || transfer_endpoint != endpoint_id ||
            transfer_trb_pointer != expected_trb)
            return false;
    } else {
        transfer_slot = slot_id;
        transfer_endpoint = endpoint_id;
        transfer_trb_pointer = expected_trb;
        __atomic_store_n(&transfer_completed, false, __ATOMIC_RELAXED);
        __atomic_store_n(&transfer_waiting, true, __ATOMIC_RELEASE);
    }


    u32 i = 0;
    while (!__atomic_load_n(&transfer_completed, __ATOMIC_ACQUIRE)) {
        xhci_process_events();

        if (i >= timeout) {
            __atomic_store_n(&transfer_waiting, false, __ATOMIC_RELEASE);
            return false;
        }
        wait_ms(timer, 1);
        i++;
    }

    *result = transfer_completion;
    __atomic_store_n(&transfer_waiting, false, __ATOMIC_RELEASE);

    return true;
}


static u8 usb_address = 0;
static u16 initial_mps = 0;
static u8 speed_id = 0;

static u64 context_size = 0;

usb_device_descriptor xhci_controller::identify_device(u8 port, TimerSource &timer) {
    usb_device_descriptor device_descriptor = {0};
    u8 slot_id = enable_device_slot(port, timer);
    if (slot_id == 0)
        return device_descriptor;
        
    volatile xhci_cap_regs *cap = (volatile xhci_cap_regs *)controller.io_base;

    u32 hccparams1 = cap->hccparams1;
    context_size = (hccparams1 & CSZ) ? 64 : 32;

    bool addr_64 = (hccparams1 & 1u) != 0;  // ac64
    addr64 = addr_64;

    u64 op_base = controller.io_base + (u64)cap->caplength;

    initial_mps = 0;

    u8 protocol_major = 0;
    read_protocol_slot_type(port, &protocol_major);
    if (!get_initial_mps(op_base, port, protocol_major, initial_mps)) {
        printf("xHCI: failed to get initial_mps from port 0x%b\n", port);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }

    dma_ret input_context = allocate_dma(context_size*33);
    dma_ret output_device_context = allocate_dma(context_size*32);

    if (input_context.status != 0) {
        printf("xHCI: failed to allocate the input context\n");
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }

    if (!addr_64 && input_context.physical_address > 0xFFFFFFFF) {
        printf("xHCI: input_context physical address too big\n");
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }

    if (output_device_context.status != 0) {
        printf("xHCI: failed to allocate the output device context\n");
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }

    if (!addr_64 && output_device_context.physical_address > 0xFFFFFFFF) {
        printf("xHCI: output_device_context physical address too big\n");
        free_dma(output_device_context.virtual_address);
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }


    memset((void *)input_context.virtual_address, 0, context_size*33); // zero-init input_context
    memset((void *)output_device_context.virtual_address, 0, context_size*32); // zero-init output_device_context

    dma_ret alloc = {0}; // define a variable of struct dma_ret
    dma_ret *alloc_ptr = &alloc; // make a pointer to that variable

    if (!xhci_dma_alloc(alloc_ptr, EP0_SIZE, addr_64)) {
        printf("xHCI: EP0 transfer ring allocation failed\n");
        free_dma(input_context.virtual_address);
        free_dma(output_device_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);
        }
        return device_descriptor;
    }

    XhciTrb* trbs = (XhciTrb *)alloc_ptr->virtual_address;
    
    XhciTrb& link = trbs[EP0_LINK];

    u64 ring_phys = alloc_ptr->physical_address;

    link.d0 = (u32)ring_phys;  // low 32 bits of ring_phys address
    link.d1 = (u32)(ring_phys >> 32); // higher 32 bits

    // link TRB
    // Type = 6
    // Toggle Cycle = 1
    link.d3 = (6u << 10) | (1u << 1) | 1;

    u32 *ep0 = (u32 *)((u8 *)input_context.virtual_address + (2 * context_size));

    u64 dequene = alloc_ptr->physical_address | 1ULL; // DCS = 1

    ep0[2] = (u32)dequene;
    ep0[3] = (u32)(dequene >> 32);


    ep0[1] = (3u << 1) | (4u << 3) | ((u32)initial_mps << 16);

    // Average TRB Length
    ep0[4] = 8;


    u32 *icc = (u32 *)input_context.virtual_address;

    icc[0] = 0; // Drop Context Flags
    icc[1] = (1u << 0) | (1u << 1); // add Slot + EP0

    // Input Slot context: index 1
    u32 *slot = (u32 *)(input_context.virtual_address + context_size);


    speed_id = get_port_speed_id(op_base, port);


    slot[0] = ((u32)speed_id << 20) | (1u << 27);
    // Speed = bits 23:20
    // Context Entries = 1, bits 31:27

    slot[1] = ((u32)port << 16);
    // Root Hub Port Number = bits 23:16

    dcbaa[slot_id] = output_device_context.physical_address;


    XhciTrb command = {};
    u64 input_phys = input_context.physical_address;

    command.d0 = (u32)input_phys;
    command.d1 = (u32)(input_phys >> 32);

    command.d3 = (11u << 10) | ((u32)slot_id << 24);

    u64 command_phys = submit_command(command);

    XhciTrb completion = {};

    if (!wait_command(timer, command_phys, &completion, 500)) {
        printf("xHCI: Address Device Timed out\n");
        dma_cache new_cache = {0};

        new_cache.dma = new dma_ret[3];
        new_cache.error_code = new u16[3];
        new_cache.max = 3;

        new_cache.dma[0] = input_context;
        new_cache.dma[1] = output_device_context;
        new_cache.dma[2] = *alloc_ptr;

        for (int i = 0; i < 3; i++) 
            new_cache.error_code[i] = (xHCI << 8) | ADDRESS_DEVICE_TIMEOUT;

        dma_cache *cache = controller.cached.cache;
        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;

        controller.cached.cache = cache;
        controller.cached.max++;

        return device_descriptor;
    }

    if ((completion.d2 >> 24) != 1) {
        printf("xHCI: Address Device failed: %u\n", completion.d2 >> 24);
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);

            dma_cache new_cache = {0};
            new_cache.dma = new dma_ret[2];
            new_cache.error_code = new u16[2];
            new_cache.max = 2;

            new_cache.dma[0] = output_device_context;
            new_cache.dma[1] = *alloc_ptr;

            new_cache.error_code[0] = (xHCI << 8) | DISABLE_SLOT_ERROR;
            new_cache.error_code[1] = new_cache.error_code[0];

            dma_cache *cache = controller.cached.cache;

            u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
            u64 new_size = old_size + sizeof(dma_cache);

            cache = (dma_cache *)krealloc(cache, old_size, new_size);
            cache[controller.cached.max] = new_cache;

            controller.cached.cache = cache;
            controller.cached.max++;
        } else {
            dcbaa[slot_id] = 0;
            asm volatile("sfence" ::: "memory");
            free_dma(output_device_context.virtual_address);
            free_dma(alloc_ptr->virtual_address);
        }
        return device_descriptor;
    }

    volatile u32 *output_slot_context =
        (volatile u32 *)output_device_context.virtual_address;


    usb_address = output_slot_context[3] & 0xFF;

    // DWORD 3, bits 31:27 = Slot State.
    u8 slot_state = (output_slot_context[3] >> 27) & 0x1F;


    dma_ret Descriptor = {0};
    dma_ret *desc_dma = &Descriptor;

    if (!xhci_dma_alloc(desc_dma, 8, addr_64)) {
        printf("xHCI: failed to allocate the descriptor buffer\n");
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);

            dma_cache new_cache = {0};
            new_cache.dma = new dma_ret[2];
            new_cache.error_code = new u16[2];
            new_cache.max = 2;

            new_cache.dma[0] = output_device_context;
            new_cache.dma[1] = *alloc_ptr;

            new_cache.error_code[0] = (xHCI << 8) | DISABLE_SLOT_ERROR;
            new_cache.error_code[1] = new_cache.error_code[0];

            dma_cache *cache = controller.cached.cache;

            u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
            u64 new_size = old_size + sizeof(dma_cache);

            cache = (dma_cache *)krealloc(cache, old_size, new_size);
            cache[controller.cached.max] = new_cache;

            controller.cached.cache = cache;
            controller.cached.max++;
        } else {
            dcbaa[slot_id] = 0;
            asm volatile("sfence" ::: "memory");
            free_dma(output_device_context.virtual_address);
            free_dma(alloc_ptr->virtual_address);
        }
        return device_descriptor;
    }

    XhciTrb setup_trb = {};

    setup_trb.d0 = 0x01000680;
    setup_trb.d1 = 0x00080000;
    setup_trb.d2 = 8;

    setup_trb.d3 = 
        (2u << 10) | // TRB Type = Setup Stage
        (1u << 6) | // IDT = 1
        (3u << 16); // TRT = IN Data Stage

    

    XhciTrb data_trb = {};

    u64 buffer_phys = desc_dma->physical_address;

    data_trb.d0 = (u32)buffer_phys;
    data_trb.d1 = (u32)(buffer_phys >> 32);
    data_trb.d2 = 8;

    data_trb.d3 = 
        (3u << 10) | // TRB Type = Data Stage
        (1u << 16);  // DIR = IN

    
    XhciTrb status_trb = {};

    status_trb.d3 = 
        (4u << 10) | // TRB Type = Status Stage
        (1u << 5); // IOC = 1

    // Prepare TRBs without publishing their Cycle bits.
    setup_trb.d3 &= ~1u;
    data_trb.d3 &= ~1u;
    status_trb.d3 &= ~1u;

    // Arm before the first transfer can complete, including the interrupt path.
    transfer_slot = slot_id;
    transfer_endpoint = 1;
    transfer_trb_pointer = ring_phys + 2 * sizeof(XhciTrb);
    __atomic_store_n(&transfer_completed, false, __ATOMIC_RELAXED);
    __atomic_store_n(&transfer_waiting, true, __ATOMIC_RELEASE);

    trbs[0] = setup_trb;
    trbs[1] = data_trb;
    trbs[2] = status_trb;
    
    asm volatile("sfence" ::: "memory");

    // Set cycle bit on all three trbs
    trbs[0].d3 |= 1u;
    trbs[1].d3 |= 1u;
    trbs[2].d3 |= 1u;

    u64 status_trb_phys = ring_phys + (sizeof(XhciTrb) << 1);

    asm volatile("sfence" ::: "memory");

    u32 save_doorbells = doorbells[slot_id];
    doorbells[slot_id] = 1;

    XhciTrb transfer_event = {};

    if (!wait_transfer_event(timer, slot_id, 1, status_trb_phys, &transfer_event, 500)) {
        printf("xHCI: GET DESCRIPTOR for port 0x%b timed out\n", port);
        
        free_dma(input_context.virtual_address);
        dma_cache new_cache = {0};
        new_cache.dma = new dma_ret[3];
        new_cache.error_code = new u16[3];
        new_cache.max = 3;

        new_cache.dma[0] = output_device_context;
        new_cache.dma[1] = *alloc_ptr;
        new_cache.dma[2] = *desc_dma;

        new_cache.error_code[0] = (xHCI << 8) | GET_DESC_TIMEOUT;
        new_cache.error_code[1] = new_cache.error_code[0];
        new_cache.error_code[2] = new_cache.error_code[1];

        dma_cache *cache = controller.cached.cache;

        u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
        u64 new_size = old_size + sizeof(dma_cache);

        cache = (dma_cache *)krealloc(cache, old_size, new_size);
        cache[controller.cached.max] = new_cache;

        controller.cached.cache = cache;
        controller.cached.max++;
            
        
        return device_descriptor;
    }

    u8 completion_code = transfer_event.d2 >> 24;

    if (completion_code != 1) {
        printf("xHCI: GET_DESCRIPTOR on port 0x%b failed: 0x%b\n", port,
            completion_code);
        free_dma(desc_dma->virtual_address);
        free_dma(input_context.virtual_address);
        if (!disable_device_slot(slot_id, timer)) {
            printf("xHCI: failed to disable slot_id 0x%b\n", slot_id);

            dma_cache new_cache = {0};
            new_cache.dma = new dma_ret[2];
            new_cache.error_code = new u16[2];
            new_cache.max = 2;

            new_cache.dma[0] = output_device_context;
            new_cache.dma[1] = *alloc_ptr;

            new_cache.error_code[0] = (xHCI << 8) | DISABLE_SLOT_ERROR;
            new_cache.error_code[1] = new_cache.error_code[0];

            dma_cache *cache = controller.cached.cache;

            u64 old_size = (u64)(controller.cached.max * sizeof(dma_cache));
            u64 new_size = old_size + sizeof(dma_cache);

            cache = (dma_cache *)krealloc(cache, old_size, new_size);
            cache[controller.cached.max] = new_cache;

            controller.cached.cache = cache;
            controller.cached.max++;
        } else {
            dcbaa[slot_id] = 0;
            asm volatile("sfence" ::: "memory");
            free_dma(output_device_context.virtual_address);
            free_dma(alloc_ptr->virtual_address);
        }
        return device_descriptor;
    }


    usb_device_descriptor *desc = (usb_device_descriptor *)desc_dma->virtual_address;

    if (desc->bDescriptorType != 1 || desc->bLength != 18) {
        printf("xHCI: invalid device descriptor from port 0x%b\n", port);
        return device_descriptor;
    }

    usb_device_descriptor *desc_ptr = &device_descriptor;

    memcpy(desc_ptr, desc, 8);
    free_dma(desc_dma->virtual_address);
    free_dma(input_context.virtual_address);

    slots[port-1].port = port;
    slots[port-1].slot_id = slot_id;
    slots[port-1].address = usb_address;
    slots[port-1].speed_id = speed_id;
    slots[port-1].superspeed = protocol_major >= 3 ||
                                (protocol_major == 0 && speed_id == 4);
    slots[port-1].ep0_mps = initial_mps;
    slots[port-1].ep0_index = 3;
    slots[port-1].ep0_cycle = 1;
    slots[port-1].EP0_physical = alloc_ptr->physical_address;
    slots[port-1].EP0_virtual = alloc_ptr->virtual_address;
    slots[port-1].output_context_physical = output_device_context.physical_address;
    slots[port-1].output_context_virtual = output_device_context.virtual_address;

    slots[port-1].output_context = output_device_context;
    slots[port-1].EP0 = *alloc_ptr;
    return device_descriptor;
}


usb_device xhci_controller::set_address(usb_device_descriptor &desc, TimerSource &timer, u8 port) {
    usb_device device = {};
    if (!slots || port == 0 || port > ports_count)
        return device;

    usb_slot &slot = slots[port - 1];
    if (slot.slot_id == 0 || slot.address == 0 || desc.bLength != 18 ||
        desc.bDescriptorType != 1)
        return device;

    // Address Device (BSR=0) already assigned the USB address in identify_device.
    device.address = slot.address;
    u16 actual_mps = desc.bMaxPacketSize0;
    if (slot.superspeed) {
        if (actual_mps != 9)
            return device;
        actual_mps = 512;
    }
    if (actual_mps == 0)
        return device;

    if (actual_mps != slot.ep0_mps) {
        auto *cap = (volatile xhci_cap_regs *)controller.io_base;
        const u32 context_bytes = (cap->hccparams1 & CSZ) ? 64 : 32;
        dma_ret input_context = {};
        if (!xhci_dma_alloc(&input_context, context_bytes * 33, addr64)) {
            printf("xHCI: EP0 Evaluate Context allocation failed\n");
            return device;
        }

        u32 *icc = (u32 *)input_context.virtual_address;
        icc[1] = 1u << 1; // Add only the EP0 context.
        u8 *input_ep0 = (u8 *)(input_context.virtual_address + 2 * context_bytes);
        const u8 *output_ep0 = (const u8 *)(slot.output_context_virtual + context_bytes);
        memcpy(input_ep0, output_ep0, context_bytes);
        u32 *ep0 = (u32 *)input_ep0;
        ep0[0] &= ~7u; // Input EP State must be zero.
        ep0[1] = (ep0[1] & 0x0000ffffu) | ((u32)actual_mps << 16);

        XhciTrb evaluate = {};
        evaluate.d0 = (u32)input_context.physical_address;
        evaluate.d1 = (u32)(input_context.physical_address >> 32);
        evaluate.d3 = TRB_TYPE(13) | ((u32)slot.slot_id << 24);
        const u64 command_phys = submit_command(evaluate);
        XhciTrb completion = {};
        if (!wait_command(timer, command_phys, &completion, 500)) {
            printf("xHCI: EP0 Evaluate Context timed out\n");
            controller.needs_reinit = true;
            // The controller might still read the input context after a timeout.
            return device;
        }
        free_dma(input_context.virtual_address);
        if ((completion.d2 >> 24) != 1) {
            printf("xHCI: EP0 Evaluate Context failed: %u\n", completion.d2 >> 24);
            return device;
        }
        slot.ep0_mps = actual_mps;
    }

    usb_setup_packet request = {0x80, GET_DESCRIPTOR, 0x0100, 0, 18};
    void *buffer = usb_send_packet(request, slot.ep0_mps, 18,
                                   slot.address, timer, port);
    if (!buffer)
        return device;

    auto *full = (usb_device_descriptor *)buffer;
    if (full->bLength == 18 && full->bDescriptorType == 1) {
        memcpy(&desc, full, sizeof(desc));
        device.descriptor = desc;
        device.discovered = true;
    } else {
        printf("xHCI: invalid full device descriptor on port 0x%b\n", port);
    }
    kfree(buffer);
    return device;
}

void *xhci_controller::usb_send_packet(usb_setup_packet packet,
                                       u32 maxpacketsize, u64 length,
                                       u8 address, TimerSource &timer, u8 port) {
    if (usb_transfer_pending() || controller.needs_reinit) return nullptr; // EP0 must not steal pending DMA/events.

    if (controller.needs_reinit || !slots || !doorbells || !dcbaa ||
        port == 0 || port > ports_count || length != packet.wLength ||
        length > 4096)
        return nullptr;

    usb_slot &slot = slots[port - 1];
    if (slot.slot_id == 0 || slot.address != address ||
        !slot.EP0_virtual || !slot.output_context_virtual ||
        dcbaa[slot.slot_id] != slot.output_context_physical ||
        maxpacketsize != slot.ep0_mps)
        return nullptr;

    const bool has_data = length != 0;
    const bool data_in = (packet.BmRequestType & IN) != 0;
    // This API has no source buffer for an OUT data stage.
    if (has_data && !data_in)
        return nullptr;

    dma_ret data_dma = {};
    if (has_data && !xhci_dma_alloc(&data_dma, length, addr64))
        return nullptr;

    XhciTrb setup = {};
    setup.d0 = (u32)packet.BmRequestType |
               ((u32)packet.bRequest << 8) |
               ((u32)packet.wValue << 16);
    setup.d1 = (u32)packet.wIndex | ((u32)packet.wLength << 16);
    setup.d2 = 8;
    setup.d3 = TRB_TYPE(TRB_SETUP) | TRB_IDT |
               ((has_data ? TRT_IN : TRT_NO_DATA) << 16);

    XhciTrb data = {};
    if (has_data) {
        data.d0 = (u32)data_dma.physical_address;
        data.d1 = (u32)(data_dma.physical_address >> 32);
        data.d2 = (u32)length;
        data.d3 = TRB_TYPE(TRB_DATA) | TRB_DIR_IN | TRB_ISP;
    }

    XhciTrb status = {};
    status.d3 = TRB_TYPE(TRB_STATUS) | TRB_IOC |
                (has_data ? 0 : TRB_DIR_IN);

    const u32 count = has_data ? 3 : 2;
    const u32 old_index = slot.ep0_index;
    const u32 old_cycle = slot.ep0_cycle;
    if (old_index > EP0_LINK) {
        if (has_data) free_dma(data_dma.virtual_address);
        return nullptr;
    }

    // Keep each control transfer contiguous. If it will not fit before the
    // Link TRB, fill the remaining entries with No-op Transfer TRBs and wrap.
    const bool wrap = old_index + count > EP0_LINK;
    const u32 first = wrap ? 0 : old_index;
    const u32 cycle = wrap ? (old_cycle ^ 1u) : old_cycle;
    auto *ring = (XhciTrb *)slot.EP0_virtual;
    const u64 status_phys = slot.EP0_physical +
                            (first + count - 1) * sizeof(XhciTrb);

    // Arm before publishing the setup TRB, so a fast completion is not lost.
    if (__atomic_load_n(&transfer_waiting, __ATOMIC_ACQUIRE)) {
        if (has_data) free_dma(data_dma.virtual_address);
        return nullptr;
    }
    transfer_slot = slot.slot_id;
    transfer_endpoint = 1;
    transfer_trb_pointer = status_phys;
    __atomic_store_n(&transfer_completed, false, __ATOMIC_RELAXED);
    __atomic_store_n(&transfer_waiting, true, __ATOMIC_RELEASE);

    status.d3 |= cycle;
    ring[first + count - 1] = status;
    if (has_data) {
        data.d3 |= cycle;
        ring[first + 1] = data;
    }
    asm volatile("mfence" ::: "memory");
    setup.d3 |= cycle;
    ring[first] = setup;

    if (wrap) {
        for (u32 i = EP0_LINK; i > old_index;) {
            --i;
            ring[i] = {0, 0, 0, TRB_TYPE(8) | old_cycle};
        }
        ring[EP0_LINK].d3 = TRB_TYPE(6) | (1u << 1) | old_cycle;
    }
    asm volatile("mfence" ::: "memory");
    doorbells[slot.slot_id] = 1; // DB Target 1: default control endpoint.

    bool completed = false;
    for (u32 elapsed = 0; elapsed < 500; ++elapsed) {
        xhci_process_events();
        if (__atomic_load_n(&transfer_completed, __ATOMIC_ACQUIRE)) {
            completed = true;
            break;
        }
        wait_ms(timer, 1);
    }
    const XhciTrb completion = transfer_completion;
    __atomic_store_n(&transfer_waiting, false, __ATOMIC_RELEASE);

    if (!completed) {
        printf("xHCI: EP0 control transfer timed out on port 0x%b\n", port);
        controller.needs_reinit = true;
        // Keep data_dma alive: the controller may still DMA into it.
        return nullptr;
    }

    u32 next_index = first + count;
    u32 next_cycle = cycle;
    if (next_index == EP0_LINK) {
        next_index = 0;
        next_cycle ^= 1u;
    }
    slot.ep0_index = next_index;
    slot.ep0_cycle = next_cycle;

    const u8 completion_code = completion.d2 >> 24;
    if (completion_code != 1 && completion_code != 13) {
        printf("xHCI: EP0 control transfer failed, code=%u\n", completion_code);
        controller.needs_reinit = true;
        // An error can stop an earlier TRB before the status stage; keep the
        // DMA buffer until the controller is reset or the slot is disabled.
        return nullptr;
    }

    // Match the other controllers: return a caller-owned buffer for IN data,
    // and a success sentinel for a completed request with no data stage.
    if (!has_data)
        return (void *)1;

    void *response = kmalloc(length);
    if (response)
        memcpy(response, (const void *)data_dma.virtual_address, length);
    free_dma(data_dma.virtual_address);
    return response;
}

usb_transfer_result xhci_controller::start_transfer(
    u32 handle, void *buffer, u32 length, TimerSource &timer)
{
    (void)timer;

    usb_transfer_result result = {};
    result.controller_type = xHCI;
    result.controller_index = controller.index;
    result.endpoint_handle = handle;
    result.status_code = FAILED;

    const u32 port_index = (handle >> 16) & 0xffu;
    const u32 interface_index = (handle >> 8) & 0xffu;
    const u32 endpoint_index = handle & 0xffu;
    if ((handle >> 24) || controller.needs_reinit || !ports || !slots ||
        !dcbaa || !doorbells || port_index >= (u32)ports_count)
        return result;
    usb_port_descriptor &port = ports[port_index];
    if (!port.connected) {
        result.status_code = NOT_CONNECTED;
        return result;
    }
    usb_device *device = port.device;
    if (!device || !device->discovered) {
        result.status_code = NOT_DISCOVERED;
        return result;
    }
    if (!device->interfaces || interface_index >= device->usb_interface_count)
        return result;
    const usb_interface &interface = device->interfaces[interface_index];
    if (interface.descriptor.bAlternateSetting != 0 || !interface.endpoints ||
        endpoint_index >= interface.endpoint_count)
        return result;
    const usb_endpoint &endpoint = interface.endpoints[endpoint_index];
    const bool in = (endpoint.address & 0x80u) != 0;
    const u32 number = endpoint.address & 0x0fu;
    if (!number || (endpoint.address & 0x70u) ||
        (endpoint.transfer_type != 2 && endpoint.transfer_type != 3) ||
        !endpoint.max_packet_size || (length && !in && !buffer))
        return result;
    if (length > endpoint.max_packet_size) {
        result.status_code = INVALID_LENGTH;
        return result;
    }
    if (!port.port_number || port.port_number > ports_count)
        return result;
    usb_slot &slot = slots[port.port_number - 1];
    const u32 dci = number * 2u + (in ? 1u : 0u);
    xhci_endpoint_ring &endpoint_ring = slot.endpoint_rings[dci];
    const u32 expected_type = endpoint.transfer_type == 2
        ? (in ? 6u : 2u) : (in ? 7u : 3u);
    if (!slot.slot_id || slot.address != device->address ||
        dcbaa[slot.slot_id] != slot.output_context_physical ||
        !endpoint_ring.configured || !endpoint_ring.dma.virtual_address ||
        endpoint_ring.endpoint_type != expected_type ||
        endpoint_ring.endpoint_address != endpoint.address ||
        endpoint_ring.enqueue_index >= EP0_LINK ||
        __atomic_load_n(&transfer_waiting, __ATOMIC_ACQUIRE))
        return result;

    // Requests are serialized with the existing
    // controller-wide event waiter. Keep DMA owned by the endpoint on timeout.
    if (!endpoint_ring.packet_dma.virtual_address &&
        !xhci_dma_alloc(&endpoint_ring.packet_dma,
                        endpoint.max_packet_size, addr64))
        return result;
    const dma_ret &packet = endpoint_ring.packet_dma;
    // One Normal TRB must not cross a 64 KiB physical boundary.
    if (length && ((packet.physical_address & 0xffffu) + length > 0x10000u))
        return result;
    if (!in && length)
        memcpy((void *)packet.virtual_address, buffer, length);

    const u32 index = endpoint_ring.enqueue_index;
    const u32 cycle = endpoint_ring.cycle;
    auto *ring = (XhciTrb *)endpoint_ring.dma.virtual_address;
    const u64 trb_phys = endpoint_ring.dma.physical_address + index * sizeof(XhciTrb);
    transfer_slot = slot.slot_id;
    transfer_endpoint = (u8)dci;
    transfer_trb_pointer = trb_phys;
    __atomic_store_n(&transfer_completed, false, __ATOMIC_RELAXED);
    __atomic_store_n(&transfer_waiting, true, __ATOMIC_RELEASE);

    // Publish the cycle bit last. Direction comes from the endpoint context.
    ring[index].d0 = (u32)packet.physical_address;
    ring[index].d1 = (u32)(packet.physical_address >> 32);
    ring[index].d2 = length;
    asm volatile("mfence" ::: "memory");
    ring[index].d3 = TRB_TYPE(1) | TRB_IOC | (in ? TRB_ISP : 0) | cycle;
    if (index + 1 == EP0_LINK)
        ring[EP0_LINK].d3 = TRB_TYPE(6) | (1u << 1) | cycle;
    asm volatile("mfence" ::: "memory");
    doorbells[slot.slot_id] = dci;

    async.endpoint = &endpoint_ring;
    async.dma_buffer = (void *)packet.virtual_address;
    async.in = in;
    transfer_phase(3);
    result.status_code = TRANSFER_PENDING;
    return result;
}

void xhci_controller::advance_transfer(TimerSource &timer) {
    xhci_process_events();
    if (!__atomic_load_n(&transfer_completed, __ATOMIC_ACQUIRE)) {
        if (async.cancel || transfer_expired(timer) || controller.needs_reinit) {
            // Stop Endpoint recovery is not implemented. Quarantine DMA and
            // reject further operations rather than reusing an owned TRB.
            controller.needs_reinit = true;
            __atomic_store_n(&transfer_waiting, false, __ATOMIC_RELEASE);
            transfer_complete(async.cancel ? TRANSFER_CANCELLED : TIMEOUT);
        }
        return;
    }
    auto &endpoint_ring = *(xhci_endpoint_ring *)async.endpoint;
    const XhciTrb completion = transfer_completion;
    __atomic_store_n(&transfer_waiting, false, __ATOMIC_RELEASE);
    async.result.hardware_status = completion.d2;
    const u32 code = completion.d2 >> 24;
    const u32 residual = completion.d2 & 0xffffffu;
    if (code != 1 && !(async.in && code == 13)) {
        controller.needs_reinit = true;
        transfer_complete(FAILED);
        return;
    }
    if (residual > async.length) {
        controller.needs_reinit = true;
        transfer_complete(INVALID_LENGTH);
        return;
    }
    if (++endpoint_ring.enqueue_index == EP0_LINK) {
        endpoint_ring.enqueue_index = 0;
        endpoint_ring.cycle ^= 1u;
    }
    async.result.actual_length = async.length - residual;
    transfer_complete(TRANSFER_SUCCESS);
}

int xhci_controller::configure_endpoints(usb_device device, u16 bConfigurationValue,
                                         u32 maxpacketsize, u8 address,
                                         TimerSource &timer) {
    if (controller.needs_reinit || !slots || !dcbaa ||
        device.port_number == 0 || device.port_number > ports_count ||
        !device.interfaces || bConfigurationValue == 0 ||
        bConfigurationValue != device.config.bConfigurationValue ||
        address != device.address)
        return 1;

    usb_slot &slot = slots[device.port_number - 1];
    if (!slot.slot_id || slot.address != address ||
        slot.ep0_mps != maxpacketsize || !slot.output_context_virtual ||
        dcbaa[slot.slot_id] != slot.output_context_physical)
        return 1;

    auto *cap = (volatile xhci_cap_regs *)controller.io_base;
    const u32 context_bytes = (cap->hccparams1 & CSZ) ? 64 : 32;
    dma_ret input_context = {};
    if (!xhci_dma_alloc(&input_context, context_bytes * 33, addr64))
        return 1;

    dma_ret pending_rings[32] = {};
    auto discard_pending = [&]() {
        for (u32 dci = 2; dci < 32; ++dci)
            if (pending_rings[dci].virtual_address)
                free_dma(pending_rings[dci].virtual_address);
        free_dma(input_context.virtual_address);
    };

    u32 *control = (u32 *)input_context.virtual_address;
    control[0] = 0;  // Drop Context flags.
    control[1] = 1u; // Add the Slot Context; EP0 is already enabled.

    u32 *input_slot = (u32 *)(input_context.virtual_address + context_bytes);
    memcpy(input_slot, (void *)slot.output_context_virtual, context_bytes);
    input_slot[3] &= ~(0x1fu << 27); // Slot State is output-only.

    u32 highest_dci = 1;
    for (u16 i = 0; i < device.usb_interface_count; ++i) {
        const usb_interface &interface = device.interfaces[i];
        if (interface.descriptor.bAlternateSetting != 0)
            continue; // SET_INTERFACE is needed before another alternate setting.
        if (interface.endpoint_count && !interface.endpoints) {
            discard_pending();
            return 1;
        }

        for (u16 j = 0; j < interface.endpoint_count; ++j) {
            const usb_endpoint &endpoint = interface.endpoints[j];
            const u8 endpoint_number = endpoint.address & 0x0fu;
            const bool in = (endpoint.address & 0x80u) != 0;
            const u8 transfer_type = endpoint.transfer_type;
            const u16 packet_size = endpoint.max_packet_size;

            // Isochronous transfers need their own TD and bandwidth path.
            if (endpoint_number == 0 || (endpoint.address & 0x70u) ||
                transfer_type == 1 || packet_size == 0 || packet_size > 1024) {
                printf("xHCI: unsupported endpoint 0x%b, type=%u\n",
                       endpoint.address, transfer_type);
                discard_pending();
                return 1;
            }

            // Extra control endpoints are bidirectional and use the IN context.
            const u32 dci = 2u * endpoint_number + (in || transfer_type == 0);
            if ((control[1] & (1u << dci)) || slot.endpoint_rings[dci].configured) {
                discard_pending();
                return 1;
            }

            u32 ep_type = 4; // Control.
            if (transfer_type == 2) ep_type = in ? 6 : 2; // Bulk.
            if (transfer_type == 3) ep_type = in ? 7 : 3; // Interrupt.

            u32 interval = 0;
            if (transfer_type == 3) {
                if (!endpoint.interval) {
                    discard_pending();
                    return 1;
                }
                if (slot.superspeed || slot.speed_id == 3) {
                    if (endpoint.interval > 16) {
                        discard_pending();
                        return 1;
                    }
                    interval = endpoint.interval - 1;
                } else {
                    // Full/low speed bInterval is in frames; xHCI uses log2 microframes.
                    u32 microframes = (u32)endpoint.interval * 8u;
                    while (microframes >>= 1)
                        ++interval;
                }
            }

            u32 max_burst = 0;
            u32 max_esit_payload = 0;
            if (slot.superspeed) {
                if (!endpoint.has_ss_companion ||
                    (transfer_type == 2 && (endpoint.ss_companion.bmAttributes & 0x1fu))) {
                    printf("xHCI: endpoint 0x%b needs unsupported SuperSpeed streams or companion\n",
                           endpoint.address);
                    discard_pending();
                    return 1;
                }
                max_burst = endpoint.ss_companion.bMaxBurst;
                if (transfer_type == 3)
                    max_esit_payload = endpoint.ss_companion.wBytesPerInterval;
            } else if (transfer_type == 3) {
                if (slot.speed_id == 3)
                    max_burst = (endpoint.descriptor.wMaxPacketSize >> 11) & 3u;
                max_esit_payload = packet_size * (max_burst + 1u);
            }

            if (!xhci_dma_alloc(&pending_rings[dci], EP0_SIZE, addr64)) {
                discard_pending();
                return 1;
            }
            XhciTrb *ring = (XhciTrb *)pending_rings[dci].virtual_address;
            const u64 ring_phys = pending_rings[dci].physical_address;
            ring[EP0_LINK].d0 = (u32)ring_phys;
            ring[EP0_LINK].d1 = (u32)(ring_phys >> 32);
            ring[EP0_LINK].d3 = TRB_TYPE(6) | (1u << 1) | 1u;

            u32 *ep = (u32 *)(input_context.virtual_address +
                              (dci + 1u) * context_bytes);
            ep[0] = interval << 16; // EP State remains Disabled in the Input Context.
            ep[1] = (3u << 1) | (ep_type << 3) |
                    (max_burst << 8) | ((u32)packet_size << 16);
            ep[2] = (u32)ring_phys | 1u; // Dequeue Cycle State = 1.
            ep[3] = (u32)(ring_phys >> 32);
            ep[4] = (u32)packet_size | (max_esit_payload << 16);

            control[1] |= 1u << dci;
            if (dci > highest_dci)
                highest_dci = dci;
        }
    }

    if (highest_dci == 1) {
        discard_pending(); // No non-default endpoints need controller resources.
        return 0;
    }

    input_slot[0] = (input_slot[0] & ~(0x1fu << 27)) |
                    (highest_dci << 27);

    XhciTrb command = {};
    command.d0 = (u32)input_context.physical_address;
    command.d1 = (u32)(input_context.physical_address >> 32);
    command.d3 = TRB_TYPE(12) | ((u32)slot.slot_id << 24);
    const u64 command_phys = submit_command(command);
    XhciTrb completion = {};
    if (!wait_command(timer, command_phys, &completion, 500)) {
        printf("xHCI: Configure Endpoint timed out for slot %u\n", slot.slot_id);
        controller.needs_reinit = true;
        // The controller may still read the input context or transfer rings.
        return 1;
    }

    const u8 completion_code = completion.d2 >> 24;
    if (completion_code != 1) {
        printf("xHCI: Configure Endpoint failed for slot %u, code=%u\n",
               slot.slot_id, completion_code);
        discard_pending();
        return 1;
    }

    for (u32 dci = 2; dci < 32; ++dci) {
        if (!pending_rings[dci].virtual_address)
            continue;
        slot.endpoint_rings[dci].dma = pending_rings[dci];
        slot.endpoint_rings[dci].endpoint_address =
            (u8)((dci >> 1) | ((dci & 1u) ? 0x80u : 0));
        slot.endpoint_rings[dci].endpoint_type =
            (u8)((((u32 *)((u8 *)input_context.virtual_address +
                          (dci + 1u) * context_bytes))[1] >> 3) & 7u);
        slot.endpoint_rings[dci].enqueue_index = 0;
        slot.endpoint_rings[dci].cycle = 1;
        slot.endpoint_rings[dci].configured = true;
    }
    free_dma(input_context.virtual_address);
    return 0;
}


void xhci_controller::service_irq_events() {
    auto *op = (volatile xhci_op_regs *)(xhci_base +
        ((volatile xhci_cap_regs *)xhci_base)->caplength);
    if (op->usbsts & (1u << 2)) controller.needs_reinit = true;
    xhci_process_events();
}
