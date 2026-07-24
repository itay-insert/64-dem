#include "uint_definitions.h"
#include "vga.h"
#include "acpi.h"
#include "paging.h"
#include "ports.h"
#include "pci.h"
#include "path.h"


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

static void PCI_read_bytes(u8 bus, u8 device, u8 function, u8 offset,
                           u8 *destination, u8 byte_count) {
    for (u8 i = 0; i < byte_count; i++) {
        destination[i] = (u8)PCI_read(bus, device, function, offset + i, 1);
    }
}

PCI_ret PCI_get_info(u8 bus, u8 device, u8 function) {
    PCI_ret ret = {0};

    ret.Bus = bus;
    ret.Device = device;
    ret.Function = function;

    if (device >= 32 || function >= 8) {
        ret.PCI_status = PCI_STATUS_INVALID_ADDRESS;
        return ret;
    }

    ret.vendor_id = (u16)PCI_read(bus, device, function, 0x00, 2);
    if (ret.vendor_id == 0xffff) {
        ret.PCI_status = PCI_STATUS_NOT_PRESENT;
        return ret;
    }

    ret.device_id = (u16)PCI_read(bus, device, function, 0x02, 2);
    PCI_read_bytes(bus, device, function, 0x04,
                   (u8 *)&ret.common, sizeof(ret.common));

    ret.PCI_Type = ret.common.header_type & 0x7f;

    switch (ret.PCI_Type) {
        case 0:
            PCI_read_bytes(bus, device, function, 0x10,
                           (u8 *)&ret.header.header0, sizeof(ret.header.header0));
            break;

        case 1:
            PCI_read_bytes(bus, device, function, 0x10,
                           (u8 *)&ret.header.header1, sizeof(ret.header.header1));
            break;

        case 2:
            PCI_read_bytes(bus, device, function, 0x10,
                           (u8 *)&ret.header.header2, sizeof(ret.header.header2));
            break;

        default:
            ret.PCI_status = PCI_STATUS_UNSUPPORTED_HEADER;
            return ret;
    }

    ret.PCI_status = PCI_STATUS_SUCCESS;
    return ret;
}

PCI_PATH_RET Discover_BootDevice(u64 path_addr, int size) {
    PCI_PATH_RET result = {0};
    result.PCI_status = PCI_STATUS_NOT_PRESENT;

    if (path_addr == 0 || size < (int)sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
        result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
        return result;
    }

    u8 *path_address = (u8 *)path_addr;
    u8 *path_end = path_address + size;
    u8 bus = 0;

    while (path_address + sizeof(EFI_DEVICE_PATH_PROTOCOL) <= path_end) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            (EFI_DEVICE_PATH_PROTOCOL *)path_address;
        u16 length = device_path_node_length(path);

        if (length < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            path_address + length > path_end) {
            result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
            return result;
        }

        if (path->Type == EFI_DEVICE_PATH_TYPE_END &&
            path->SubType == EFI_DEVICE_PATH_SUBTYPE_END_ENTIRE)
            return result;

        if (path->Type == EFI_DEVICE_PATH_TYPE_HARDWARE &&
            path->SubType == EFI_DEVICE_PATH_SUBTYPE_PCI) {
            if (length < sizeof(PCI_DEVICE_PATH)) {
                result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
                return result;
            }

            PCI_DEVICE_PATH *pci_path = (PCI_DEVICE_PATH *)path;
            if (result.DeviceCount >= PCI_PATH_MAX_DEVICES) {
                result.PCI_status = PCI_STATUS_TOO_MANY_DEVICES;
                return result;
            }

            PCI_ret device =
                PCI_get_info(bus, pci_path->Device, pci_path->Function);
            if (device.PCI_status != PCI_STATUS_SUCCESS) {
                result.PCI_status = device.PCI_status;
                return result;
            }

            result.Devices[result.DeviceCount] = device;
            result.DeviceCount++;
            result.PCI_status = PCI_STATUS_SUCCESS;

            if (device.PCI_Type == 1)
                bus = device.header.header1.secondary_bus;
        }

        path_address += length;
    }

    return result;
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
