#include <stdbool.h>
#include "uint_definitions.h"
#include "drivers/display/vga.h"
#include "x86-64/lowlevel.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/path.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_names.h"


#define TIMEOUT 1000000

#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_CNR (1u << 11)

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
    xhci_controller_t *controller = context;

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
    *controller = (xhci_controller_t){0};

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


int xhci_init(void) {
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
    u64 virtual = BASE + physical;

    printf("Mapping xHCI...\n");

    create_mapping(virtual, physical, pages, 0x13, KernelPML4);

    flush_pages(virtual, pages);

    xhci_base = virtual + page_offset;

    int rests = xhci_reset();

    if (rests != 0)
        return 1;

   
    volatile xhci_cap_regs *cap;
    cap = (volatile xhci_cap_regs *)xhci_base;

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


        
    return 0;
}
