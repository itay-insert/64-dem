#include <stdbool.h>
#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"

#define CAP_NP 0x1f
#define CAP_NCS(x) (((x) & 0x1fff) >> 8)
#define CAP_S64A 0x80000000

#define CAP2_BOH (1u << 0)

#define BOHC_BOS (1u << 0)
#define BOHC_OOS (1u << 1)
#define BOHC_BB (1u << 4)

typedef struct {
    u32 CAP;
    u32 GHC;
    u32 IS;
    u32 PI;
    u32 VS;
    u32 Padding0;
    u32 Padding1;
    u32 Padding2;
    u32 Padding3;
    u32 CAP2;
    u32 BOHC;
} __attribute__((packed)) ahci_cap_regs;


typedef struct {
    u32 CLB;
    u32 CLBU;
    u32 FB;
    u32 FBU;
    u32 IS;
    u32 IE;
    u32 CMD;
    u32 Reserved;
    u32 TFD;
    u32 SIG;
    u32 SSTS;
    u32 SCTL;
    u32 SERR;
    u32 SACT;
    u32 CI;
} __attribute__((packed)) ahci_port;

typedef struct {
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u64 io_phys;
    u64 io_virt;
    pci_address_t address;
    bool discovered;
} ahci_descriptor;

typedef struct {
    u8 port_number;
    u8 device_type;
    bool connected;
    bool device_present;
    bool initialized;
} port_descriptor;

port_descriptor *ports = nullptr;
ahci_descriptor ahci = {0};

u8 port_count = 0;
u8 cmd_slots = 0;

bool addr_64 = false;

void ahci_find_callback(const PCI_ret *device, void *context) {
    ahci_descriptor *desc = (ahci_descriptor *)context;

    if (desc->discovered || device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != desc->class_code || 
        device->common.subclass != desc->subclass ||
        device->common.prog_if != desc->prog_if)
        return;

    printf("SATA contoller found at pci address %u:%u:%u.%u\n", device->Segment, 
    device->Bus, device->Device, device->Function);
    desc->address = (pci_address_t) {
        .segment = device->Segment,
        .bus = device->Bus,
        .device = device->Device,
        .function = device->Function
    };

    desc->discovered = true;
}


static bool discover_ahci(void) {
    ahci_descriptor *desc = &ahci;
    *desc = {};

    desc->class_code = 0x01;
    desc->subclass = 0x06;
    desc->prog_if = 0x01;

    // discover pci device
    pci_enumerate(ahci_find_callback, desc);

    if (!desc->discovered)
        return false;

    pci_bar_t bar = {0};
    if (pci_read_bar(desc->address, 5, 0, &bar) != PCI_STATUS_SUCCESS)
        return false;

    // read the address from BAR5
    u64 addr = bar.address;
    desc->io_phys = addr;
    desc->io_virt = desc->io_phys + BASE;

    u64 page_phys = addr & ~4095ULL;
    u64 offset = addr - page_phys;

    u64 pages = (offset + bar.size + 4095) >> 12;

    // map the address to the MMIO region
    create_mapping(page_phys+BASE, page_phys, pages, 0x13, KernelPML4);
    flush_pages(page_phys+BASE, pages);

}


static inline bool ahci_take_ownership(TimerSource &timer, volatile ahci_cap_regs *regs) {
    if (!(regs->CAP2 & CAP2_BOH))
        return true;

    regs->BOHC = (regs->BOHC & ~BOHC_OOS) | BOHC_OOS; // set OOS bit
    
    bool bios_busy = false;
    for (u32 elapsed = 0; elapsed < 25; elapsed++) { // 25 ms timeout for BOHC_BB
        if (!(regs->BOHC & BOHC_BOS))
            return true;

        if (regs->BOHC & BOHC_BB) {
            bios_busy = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!bios_busy) {
        if (!(regs->BOHC & BOHC_BOS))
            return true;

        if (!(regs->BOHC & BOHC_BB))
            return true;

        bios_busy = true;
    }


    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (!(regs->BOHC & BOHC_BOS))
            return true; // handoff completed successfully
        
        wait_ms(timer, 1);
    }

   return !(regs->BOHC & BOHC_BOS);
}


static inline u64 port_to_address(u32 port) {
    u64 port_base = ahci.io_virt + 0x100 + (port * 0x80);
    return port_base;
}

#define SCTL_DET_MASK 0xF
#define SCTL_DET_OFF 4

#define CMD_ST (1u << 0)
#define CMD_CR (1u << 15)

static bool ahci_enumrate_port(u32 port, TimerSource &timer) {
    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port);

    u32 det = port_reg->SSTS & 0xf;

    switch (det) {
        case 0:
            ports[port] = {0};
            ports[port].device_present = false;
            break;

        case 1:
            u32 sctl = port_reg->SCTL;
            // assert COMRESET
            port_reg->SCTL = (sctl & ~0xFu) | 1u;

            wait_ms(timer, 1);

            port_reg->SCTL = sctl & ~0xFu;

            for (u32 elapsed = 0; elapsed < 1000; elapsed++) {
                if ((port_reg->SSTS & 0xF) == 3) {
                    ports[port].connected = true;
                    break;
                }

                wait_ms(timer, 1);
            }

            if (!ports[port].connected) {
                printf("SATA: link initialization for port 0x%b failed or timed out\n", port);
                return false;
            }

            port->SERR = 0xFFFFFFFF;

            goto next;

        case 2:
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;

        case 3:
            goto next;

        case 4:
            if ((sctl & SCTL_DET_MASK) == SCTL_DET_OFF) {
                // software disabled the SATA PHY.
                u32 cmd = port_reg->CMD; // read the port's command register
                
                if (cmd & CMD_ST) {
                    port->CMD = cmd & ~CMD_ST;

                    bool stopped = false;
                    for (u32 i = 0; i < 1000; i++) {// 1 second timeout
                        if (!(port->CMD & CMD_CR)) {
                            stopped = true;
                            break;
                        }

                        wait_ms(timer, 1);
                    }

                    if (!stopped)
                        return false;
                }

                u32 sctl = port_reg->SCTL;
                port_reg->SCTL = sctl & ~SCTL_DET_MASK;

                for (u32 i = 0; i < 2000; i++) {

                    u32 det = port_reg->SSTS & 0xF;

                    if (det == 3)
                        goto next;

                    wait_ms(timer, 1);
                }

                return false;
            } else {
                printf("SATA: offline condition disabled the SATA PHY\n");
                return false;
            }
            
        default:
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;
    }

    next:

    u32 sig = port->SIG;

    if (sig == 0xFFFFFFFF) {
        // signature haven't been recieved, try reset
    } else {
        switch (sig) {
            case 0x00000101:
                // SATA ATA device
                // TODO: make identify_device
                break;
            
            case 0xEB140101:
                // ATAPI device

                break;

            case 0xC33C0101:
                // Enclousure management bridge

                break;

            case 0x96690101:
                // port multiplier

                break;

            default:

                break;
        }
    }
    
}

bool controller_init(TimerSource &timer) {
    if (!discover_ahci())
        return false;

    
    volatile ahci_cap_regs *cap_regs = (volatile ahci_cap_regs *)ahci.io_virt;

    u32 cap = cap_regs->CAP;
    port_count = (u32)(cap & CAP_NP); // get number of ports - 1
    port_count++;

    ports = new port_descriptor[port_count];

    cmd_slots = (u8)CAP_NCS(cap); // get number of command slots - 1
    addr_64 = (cap & CAP_S64A);  // get whether 64-bit dma addresses are supported

    if (!ahci_take_ownership(timer, cap_regs)) {
        printf("SATA: failed to obtain ownership\n");
        return false;
    }

    cap_regs->GHC |= (1u << 31); // enable ahci mode



}