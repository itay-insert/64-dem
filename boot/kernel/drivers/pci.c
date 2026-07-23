#include "uint_definitions.h"
#include "vga.h"
#include "acpi.h"
#include "paging.h"
#include "ports.h"


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

typedef struct {
    uint16_t command;
    uint16_t status;

    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;

    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
} __attribute__((packed)) PCICommonHeader;

typedef struct {

    uint32_t bar[6];               // 0x10-0x27

    uint32_t cardbus_cis_ptr;      // 0x28

    uint16_t subsystem_vendor_id;  // 0x2C
    uint16_t subsystem_id;         // 0x2E

    uint32_t expansion_rom;        // 0x30

    uint8_t capabilities_ptr;      // 0x34
    uint8_t reserved1[3];

    uint32_t reserved2;            // 0x38

    uint8_t interrupt_line;        // 0x3C
    uint8_t interrupt_pin;         // 0x3D
    uint8_t min_grant;             // 0x3E
    uint8_t max_latency;           // 0x3F
} __attribute__((packed)) PCIHeaderType0;

typedef struct {

    uint32_t bar[2];                  // 0x10

    uint8_t primary_bus;              // 0x18
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t secondary_latency;

    uint8_t io_base;
    uint8_t io_limit;

    uint16_t secondary_status;

    uint16_t memory_base;
    uint16_t memory_limit;

    uint16_t prefetch_base;
    uint16_t prefetch_limit;

    uint32_t prefetch_base_upper32;
    uint32_t prefetch_limit_upper32;

    uint16_t io_base_upper16;
    uint16_t io_limit_upper16;

    uint8_t capabilities_ptr;
    uint8_t reserved[3];

    uint32_t expansion_rom;

    uint8_t interrupt_line;
    uint8_t interrupt_pin;

    uint16_t bridge_control;
} __attribute__((packed)) PCIHeaderType1;


typedef struct {

    uint32_t cardbus_socket_base;

    uint8_t capabilities_ptr;
    uint8_t reserved;

    uint16_t secondary_status;

    uint8_t pci_bus;
    uint8_t cardbus_bus;
    uint8_t subordinate_bus;
    uint8_t cardbus_latency;

    uint32_t memory_base0;
    uint32_t memory_limit0;

    uint32_t memory_base1;
    uint32_t memory_limit1;

    uint32_t io_base0;
    uint32_t io_limit0;

    uint32_t io_base1;
    uint32_t io_limit1;

    uint8_t interrupt_line;
    uint8_t interrupt_pin;

    uint16_t bridge_control;

    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;

    uint32_t legacy_base;
} __attribute__((packed)) PCIHeaderType2;


typedef struct {
    int PCI_status;
    int PCI_Type;
    u8 Bus;
    u8 Device;
    u8 Function;
    u16 vendor_id;
    u16 device_id;
    
    PCICommonHeader common;

    union {
        PCIHeaderType0 header0;
        PCIHeaderType1 header1;
        PCIHeaderType2 header2;
    } header;
} PCI_ret;

u8 Express_enabled = 0;
int BusCount = 0;


u32 PCI_read(u8 bus, u8 device, u8 function, u8 offset, u8 size) {
    u32 address;
    u32 value;

    if (Express_enabled) {

        volatile u8 *ptr = (volatile u8 *)
            (PCI_BASE +
            ((u32)bus << 20) |
            ((u32)device << 15) |
            ((u32)function << 12) |
            offset);

        switch(size) {
            case 1:
                return *(volatile u8 *)ptr;

            case 2:
                return *(volatile u16 *)ptr;

            case 4:
                return *(volatile u32 *)ptr;
        }

        return 0;
    }

    address =
        (1 << 31) |
        ((u32)bus << 16) |
        ((u32)device << 11) |
        ((u32)function << 8) |
        (offset & 0xFC);


    outl(0xCF8, address);

    value = inl(0xCFC);


    switch(size) {
        case 1:
            return (value >> ((offset & 3) * 8)) & 0xFF;

        case 2:
            return (value >> ((offset & 2) * 8)) & 0xFFFF;

        case 4:
            return value;
    }


    return 0;
}

int PCI_list(void) {
    int count = 0;

    for (u16 bus = 0; bus < (u16)BusCount; bus++) {
        for (u8 device = 0; device < 32; device++) {
            u16 vendor = PCI_read(
                bus,
                device,
                0,
                0x00,
                2
            );

            if (vendor == 0xFFFF)
                continue;


            count++;

            u8 header_type = PCI_read(
                bus,
                device,
                0,
                0x0E,
                1
            );


            if (header_type & 0x80) {
                for (u8 function = 1; function < 8; function++) {
                    vendor = PCI_read(
                        bus,
                        device,
                        function,
                        0x00,
                        2
                    );

                    if (vendor != 0xFFFF)
                        count++;
                }
            }
        }
    }

    return count;
}

void pci_init(void) {
    ACPI_ret ret = ACPI_discovery("MCFG");
    if (ret.status == 0) {
        printf("PCI Express detected\n");
        Express_enabled = 1;
        MCFG *mcfg = (MCFG *)ret.Address;
        u32 count = (mcfg->header.length - sizeof(MCFG)) / sizeof(MCFGEntry);
        MCFGEntry *entries = (MCFGEntry *)((u8 *)mcfg + sizeof(MCFG));
        u64 size_mapped = 0;
        for (u32 i = 0; i < count; i++) {
            u64 bus_count = entries[i].EndBus - entries[i].StartBus + 1;
            BusCount += (int)bus_count;
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
        int PCI_devices = PCI_list();
        printf("%d PCI Devices detected\n", PCI_devices);
    } else {
        printf("No PCI Express detected, falling back to PCI legacy\n");
        Express_enabled = 0;
        BusCount = 256;
        printf("PCI AddressPort = 0xCF8    PCI DataPort = 0xCFC\n");
        int PCI_devices = PCI_list();
        printf("%d PCI Devices detected\n", PCI_devices);
    }
}