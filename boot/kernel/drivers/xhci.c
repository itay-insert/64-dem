#include "uint_definitions.h"
#include "drivers/display/vga.h"
#include "x86-64/lowlevel.h"
#include "x86-64/paging.h"


extern u64 xhci_base;

typedef struct __attribute__((packed)) {
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


typedef struct __attribute__((packed)) {
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

void xchi_init(void) {
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

    volatile uint32_t xhci_doorbells = (volatile uint32_t *)
            (xhci_base + xhci_cap->dboff);

    volatile uint8_t xhci_runtime = (volatile uint8_t *)
            (xhci_base + xhci_cap->rtsoff);

    
    while (op->usbsts & (1 << 11));

    op->usbcmd &= ~1;

    while (!(op->usbsts & 1));

    op->usbcmd |= (1 << 1);

    while (op->usbcmd & (1 << 1));

    while (op->usbsts & (1 << 11));

    

    
}
