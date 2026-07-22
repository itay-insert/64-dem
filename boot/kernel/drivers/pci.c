#include "uint_definitions.h"
#include "vga.h"
#include "acpi.h"
#include "paging.h"


typedef struct {
    ACPISDTHeader header;
    u64 reserved;
} __attribute__((packed)) MCFG;

typedef struct {
    u64 BaseAddress;
    u16 SegmentGroup;
    u8 StartBus;
    u8 EndBus;
    u32 Reserved;
} __attribute__((packed)) MCFGEntry;


void pci_init(void) {
    ACPI_ret ret = ACPI_discovery("MCFG");
    if (ret.status == 0) {
        printf("PCI Express detected\n");
        MCFG *mcfg = (MCFG *)ret.Address;
        u32 count = (mcfg->header.length - sizeof(MCFG)) / sizeof(MCFGEntry);
        MCFGEntry *entries = (MCFGEntry *)((u8 *)mcfg + sizeof(MCFG));
        u64 size_mapped = 0;
        for (u32 i = 0; i < count; i++) {
            u64 bus_count = entries[i].EndBus - entries[i].StartBus + 1;
            u64 page_count = bus_count << 8;
            create_mapping(PCI_BASE+size_mapped,
                entries[i].BaseAddress, 
                page_count, 
                0x13, 
            (u64 *)KernelPML4);
            flush_pages(PCI_BASE+size_mapped, page_count);
            printf("PhysicalBase = 0x%lx  VirtualBase = 0x%lx\n", entries[i].BaseAddress, 
                PCI_BASE+size_mapped);
            printf("Segment = %u\n", (unsigned int)entries[i].SegmentGroup);
            printf("Bus %u - %u\n", (unsigned int)entries[i].StartBus, 
            (unsigned int)entries[i].EndBus);
            size_mapped += (page_count << 12);
        }
    } else {
        printf("No PCI Express detected, falling back to PCI legacy\n");
        printf("PCI AddressPort = 0xCF8    PCI DataPort = 0xCFC\n");
    }
}