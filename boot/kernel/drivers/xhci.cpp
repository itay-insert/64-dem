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
#include "drivers/xhci_events.h"

#define TIMEOUT 1000000

#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_CNR (1u << 11)


#define XHCI_EXT_CAP_LEGACY 1
#define XHCI_LEGACY_BIOS_OWNED (1u << 16)
#define XHCI_LEGACY_OS_OWNED (1u << 24)

#define XHCI_LEGACY_SMI_ENABLES ((1u << 0) | (1u << 4) | (1u << 13) | (1u << 14) | (1u << 15))

typedef struct {
    bool found;
    pci_address_t pci_address;
    pci_bar_t bar;
} xhci_controller_t;


u64 xhci_base = 0;
u64 *scratch_pad = 0;
u64 *dcbaa = 0;
u64 scratchpad_physical = 0;

xhci_controller_t xhci_controller = {0};



typedef struct {
    uint8_t  caplength;
    uint8_t  reserved;
    uint16_t hciversion;

    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;

    uint32_t hccparams1;

    uint32_t dboff;
    uint32_t rtsoff;

    uint32_t hccparams2;
} xhci_cap_regs;


typedef struct {
    uint32_t usbcmd;        
    uint32_t usbsts;        
    uint32_t pagesize;      
    uint32_t rsvd0[2];      
    uint32_t dnctrl;        
    uint64_t crcr;          
    uint32_t rsvd1[4];      
    uint64_t dcbaap;        
    uint32_t config;        
    uint32_t rsvd2[241];
} xhci_op_regs;


static void xhci_find_callback(const PCI_ret *device, void *context) {
    xhci_controller_t *controller = static_cast<xhci_controller_t *>(context);

    if (controller->found ||
        device->PCI_status != PCI_STATUS_SUCCESS)
        return;


    if (device->common.class_code != 0x0C ||
        device->common.subclass != 0x03 ||
        device->common.prog_if != 0x30)
        return;


    controller->pci_address = (pci_address_t) {
        .segment  = device->Segment,
        .bus      = device->Bus,
        .device   = device->Device,
        .function = device->Function,
    };

    controller->found = true;

}


bool xhci_discover(xhci_controller_t *controller) {
    *controller = {};

    pci_enumerate(xhci_find_callback, controller);

    if (!controller->found) {
        printf("xHCI: no controller found\n");
        return false;
    }

    printf("xHCI found at %u:%u:%u.%u\n",
            (unsigned int)controller->pci_address.segment,
            (unsigned int)controller->pci_address.bus,
            (unsigned int)controller->pci_address.device,
            (unsigned int)controller->pci_address.function);

    return true;
}




static bool xhci_wait_clear(volatile u32 *reg, u32 mask) {
    u32 attempts = TIMEOUT;

    while (attempts--) {
        if ((*reg & mask) == 0)
            return true;

        __asm__ __volatile__("pause");
    }

    return false;
}



static bool xhci_wait_set(volatile u32 *reg, u32 mask)  {
    u32 attempts = TIMEOUT;

    while (attempts--) {
        if ((*reg & mask) == mask)
            return true;

        __asm__ __volatile__("pause");
    }

    return false;
}





int xhci_reset(void) {
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


int allocate_scracthpad(u32 scratchpad_count, bool addr_64) {
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

static volatile u32 *xhci_find_extended_capability(volatile xhci_cap_regs *cap, 
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


static bool xhci_take_ownership(volatile xhci_cap_regs *cap, TimerSource& timer) {
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
        bytes > 0x100000000ULL - out->physical_address))
        return false;

    memset(reinterpret_cast<void *>(out->virtual_address), 0, bytes);
    return true;
}


struct XhciTrb {
    u32 d0, d1, d2, d3;
};

static_assert(sizeof(XhciTrb) == 16);

struct XhciErstEntry {
    u64 segment_base;
    u32 segment_size_trbs;
    u32 reserved;
};
static_assert(sizeof(XhciErstEntry) == 16);

struct XhciRings {
    dma_ret command_dma;
    dma_ret event_dma;
    dma_ret erst_dma;

    XhciTrb *command;
    XhciTrb *event;

    u64 doorbell_base;
    u64 interrupter0_base;
    bool ac64;

    u32 command_index = 0;
    u32 command_cycle = 1;
    u32 event_index = 0;
    u32 event_cycle = 1;
};

static XhciRings rings; // Keep this state after xhci_init() returns.

/* The current command ring permits one outstanding command at a time. */
static u64 pending_command_physical = 0;
static XhciTrb pending_command_completion = {};
static u32 pending_command_done = 0;
static u32 interrupt_driven = 0;
static u32 processing_events = 0;
static u32 pending_port_changes[8] = {};

static bool setup_rings(volatile xhci_cap_regs *cap,
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

static u64 submit_command(XhciTrb command) {
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



static bool poll_event(XhciTrb *out) {
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

extern "C" u32 xhci_process_events(void) {
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
        } else if (type == 32) { // Transfer Event
            // Add endpoint completion dispatch when transfer rings exist.
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

extern "C" int xhci_take_port_change(u8 port_number) {
    if (port_number == 0)
        return 0;
    const u32 mask = 1u << (port_number & 31);
    return (__atomic_fetch_and(&pending_port_changes[port_number >> 5],
                               ~mask, __ATOMIC_ACQ_REL) & mask) != 0;
}

extern "C" void xhci_set_interrupt_driven(int enabled) {
    __atomic_store_n(&interrupt_driven, enabled != 0, __ATOMIC_RELEASE);
}



static bool wait_command(TimerSource& timer, u64 command_physical,
                           XhciTrb *completion, u32 timeout_ms) {
    const u64 frequency = timer.read_freq();
    if (frequency == 0) return false;

    const u64 limit = (frequency * timeout_ms + 999) / 1000;
    u32 previous = static_cast<u32>(timer.read()) & 0x00ffffffu;
    u64 elapsed = 0;

    while (elapsed < limit) {
        if (!__atomic_load_n(&interrupt_driven, __ATOMIC_ACQUIRE))
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


int xhci_init(TimerSource& timer) {
    xhci_controller_t *controller = &xhci_controller;

    if (!xhci_discover(controller))
        return 1;

    if (pci_enable_device(controller->pci_address) != PCI_STATUS_SUCCESS) {
        printf("xHCI: failed to enable device\n");
        return 1;
    }

    if (pci_enable_bus_mastering(controller->pci_address) != PCI_STATUS_SUCCESS) {
        printf("xHCI: failed to enable bus mastering\n");
        return 1;
    }


    if (pci_read_bar(controller->pci_address, 0, 1, &controller->bar) != PCI_STATUS_SUCCESS) {
        printf("xHCI: failed to read BAR0\n");
        return 1;
    }

    if (controller->bar.type != PCI_BAR_MEMORY ||
        controller->bar.address == 0 || controller->bar.size == 0) {
        printf("xHCI: invalid BAR0\n");
        return 1;
    }

    u64 page_offset = controller->bar.address & 0xfffULL;
    u64 pages = (page_offset + controller->bar.size + 0xfffULL) >> 12;

    printf("xHCI MMIO physical address: 0x%lx\n", controller->bar.address);

    u64 physical = controller->bar.address & ~0xfffULL;
    u64 virtual_address = BASE + physical;

    printf("Mapping xHCI...\n");

    create_mapping(virtual_address, physical, pages, 0x13, KernelPML4);

    flush_pages(virtual_address, pages);

    xhci_base = virtual_address + page_offset;
   
    volatile xhci_cap_regs *cap;
    cap = (volatile xhci_cap_regs *)xhci_base;

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

    u8 enabled_slots = max_slots < 8 ? max_slots : 8;
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

static inline u32 read_portsc(u64 base, u8 reg) {
    return MMIO_read32(base, (reg * 0x10));
}

#define PORTSC_CCS (1u << 0)

void USB_init(void) {
    volatile xhci_cap_regs *cap = (volatile xhci_cap_regs *)xhci_base;

    u64 op_base = xhci_base + cap->caplength;
    u32 hcsparams1 = cap->hcsparams1;
    u8 max_ports = (hcsparams1 >> 24) & 0xFF;
    
    u64 ports_base = op_base + 0x400;

    for (u8 port = 1; port <= max_ports; port++) {
        u32 portsc = read_portsc(ports_base, port);

        if (portsc & PORTSC_CCS) {
            printf("USB Device connected on port 0x%b\n", port);
        }
    }
    
}