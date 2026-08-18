#include "uint_definitions.h"
#include "drivers/display/vga.h"
#include "x86-64/acpi.h"
#include "x86-64/paging.h"
#include "x86-64/ports.h"
#include "x86-64/spinlock.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/path.h"

#define PCI_MAX_ECAM_REGIONS 32
#define PCI_CONFIG_LEGACY_SIZE 256
#define PCI_CONFIG_PCIE_SIZE 4096

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
    u64 physical_base;
    u64 virtual_base;
    u16 segment;
    u8 start_bus;
    u8 end_bus;
} pci_ecam_region_t;

u8 Express_enabled = 0;
int BusCount = 0;

static pci_ecam_region_t ecam_regions[PCI_MAX_ECAM_REGIONS];
static u32 ecam_region_count;
static spinlock_t legacy_config_lock;

static int valid_access(pci_address_t address, u16 offset, u8 size,
                        u16 config_size) {
    if (address.device >= 32 || address.function >= 8)
        return 0;
    if (size != 1 && size != 2 && size != 4)
        return 0;
    if ((u32)offset + size > config_size)
        return 0;
    if ((size == 2 && (offset & 1)) || (size == 4 && (offset & 3)))
        return 0;
    return 1;
}

static pci_ecam_region_t *find_ecam_region(u16 segment, u8 bus) {
    for (u32 i = 0; i < ecam_region_count; i++) {
        pci_ecam_region_t *region = &ecam_regions[i];
        if (region->segment == segment && bus >= region->start_bus &&
            bus <= region->end_bus)
            return region;
    }
    return 0;
}

static volatile u8 *ecam_pointer(pci_ecam_region_t *region,
                                 pci_address_t address, u16 offset) {
    u64 relative_bus = (u64)(address.bus - region->start_bus);
    return (volatile u8 *)(region->virtual_base + (relative_bus << 20) +
                           ((u64)address.device << 15) +
                           ((u64)address.function << 12) + offset);
}

int pci_config_read(pci_address_t address, u16 offset, u8 size, u32 *value) {
    if (!value)
        return PCI_STATUS_INVALID_ADDRESS;

    pci_ecam_region_t *region = find_ecam_region(address.segment, address.bus);
    if (region) {
        if (!valid_access(address, offset, size, PCI_CONFIG_PCIE_SIZE))
            return PCI_STATUS_INVALID_ADDRESS;
        volatile u8 *pointer = ecam_pointer(region, address, offset);
        if (size == 1)
            *value = *(volatile u8 *)pointer;
        else if (size == 2)
            *value = *(volatile u16 *)pointer;
        else
            *value = *(volatile u32 *)pointer;
        return PCI_STATUS_SUCCESS;
    }

    if (address.segment != 0 ||
        !valid_access(address, offset, size, PCI_CONFIG_LEGACY_SIZE))
        return PCI_STATUS_INVALID_ADDRESS;

    u32 config_address = (1u << 31) | ((u32)address.bus << 16) |
                         ((u32)address.device << 11) |
                         ((u32)address.function << 8) | (offset & 0xfc);
    spin_lock(&legacy_config_lock);
    outl(0xcf8, config_address);
    u32 data = inl(0xcfc);
    spin_unlock(&legacy_config_lock);

    if (size == 1)
        *value = (data >> ((offset & 3) * 8)) & 0xff;
    else if (size == 2)
        *value = (data >> ((offset & 2) * 8)) & 0xffff;
    else
        *value = data;
    return PCI_STATUS_SUCCESS;
}

int pci_config_write(pci_address_t address, u16 offset, u8 size, u32 value) {
    pci_ecam_region_t *region = find_ecam_region(address.segment, address.bus);
    if (region) {
        if (!valid_access(address, offset, size, PCI_CONFIG_PCIE_SIZE))
            return PCI_STATUS_INVALID_ADDRESS;
        volatile u8 *pointer = ecam_pointer(region, address, offset);
        if (size == 1)
            *(volatile u8 *)pointer = (u8)value;
        else if (size == 2)
            *(volatile u16 *)pointer = (u16)value;
        else
            *(volatile u32 *)pointer = value;
        return PCI_STATUS_SUCCESS;
    }

    if (address.segment != 0 ||
        !valid_access(address, offset, size, PCI_CONFIG_LEGACY_SIZE))
        return PCI_STATUS_INVALID_ADDRESS;

    u32 config_address = (1u << 31) | ((u32)address.bus << 16) |
                         ((u32)address.device << 11) |
                         ((u32)address.function << 8) | (offset & 0xfc);
    spin_lock(&legacy_config_lock);
    outl(0xcf8, config_address);
    if (size == 4) {
        outl(0xcfc, value);
    } else {
        u32 old = inl(0xcfc);
        u32 shift = (offset & 3) * 8;
        u32 mask = (size == 1 ? 0xffu : 0xffffu) << shift;
        outl(0xcfc, (old & ~mask) | ((value << shift) & mask));
    }
    spin_unlock(&legacy_config_lock);
    return PCI_STATUS_SUCCESS;
}

u8 pci_read8(pci_address_t address, u16 offset) {
    u32 value = 0xff;
    pci_config_read(address, offset, 1, &value);
    return (u8)value;
}

u16 pci_read16(pci_address_t address, u16 offset) {
    u32 value = 0xffff;
    pci_config_read(address, offset, 2, &value);
    return (u16)value;
}

u32 pci_read32(pci_address_t address, u16 offset) {
    u32 value = 0xffffffff;
    pci_config_read(address, offset, 4, &value);
    return value;
}

int pci_write8(pci_address_t address, u16 offset, u8 value) {
    return pci_config_write(address, offset, 1, value);
}

int pci_write16(pci_address_t address, u16 offset, u16 value) {
    return pci_config_write(address, offset, 2, value);
}

int pci_write32(pci_address_t address, u16 offset, u32 value) {
    return pci_config_write(address, offset, 4, value);
}

u32 PCI_read(u8 bus, u8 device, u8 function, u16 offset, u8 size) {
    u32 value = 0xffffffff;
    pci_address_t address = {0, bus, device, function};
    pci_config_read(address, offset, size, &value);
    return value;
}

void PCI_write(u8 bus, u8 device, u8 function, u16 offset, u8 size,
               u32 value) {
    pci_address_t address = {0, bus, device, function};
    pci_config_write(address, offset, size, value);
}

static void pci_read_bytes(pci_address_t address, u16 offset,
                           u8 *destination, u16 byte_count) {
    for (u16 i = 0; i < byte_count; i++)
        destination[i] = pci_read8(address, offset + i);
}

PCI_ret pci_get_info(pci_address_t address) {
    PCI_ret ret = {0};
    ret.Segment = address.segment;
    ret.Bus = address.bus;
    ret.Device = address.device;
    ret.Function = address.function;

    if (address.device >= 32 || address.function >= 8) {
        ret.PCI_status = PCI_STATUS_INVALID_ADDRESS;
        return ret;
    }
    ret.vendor_id = pci_read16(address, 0x00);
    if (ret.vendor_id == 0xffff) {
        ret.PCI_status = PCI_STATUS_NOT_PRESENT;
        return ret;
    }
    ret.device_id = pci_read16(address, 0x02);
    pci_read_bytes(address, 0x04, (u8 *)&ret.common, sizeof(ret.common));
    ret.PCI_Type = ret.common.header_type & 0x7f;

    if (ret.PCI_Type == 0)
        pci_read_bytes(address, 0x10, (u8 *)&ret.header.header0,
                       sizeof(ret.header.header0));
    else if (ret.PCI_Type == 1)
        pci_read_bytes(address, 0x10, (u8 *)&ret.header.header1,
                       sizeof(ret.header.header1));
    else if (ret.PCI_Type == 2)
        pci_read_bytes(address, 0x10, (u8 *)&ret.header.header2,
                       sizeof(ret.header.header2));
    else {
        ret.PCI_status = PCI_STATUS_UNSUPPORTED_HEADER;
        return ret;
    }
    ret.PCI_status = PCI_STATUS_SUCCESS;
    return ret;
}

PCI_ret PCI_get_info(u8 bus, u8 device, u8 function) {
    pci_address_t address = {0, bus, device, function};
    return pci_get_info(address);
}

static int enumerate_bus(u16 segment, u8 bus,
                         pci_enumerate_callback_t callback, void *context) {
    int count = 0;
    for (u8 device = 0; device < 32; device++) {
        pci_address_t address = {segment, bus, device, 0};
        if (pci_read16(address, 0) == 0xffff)
            continue;
        u8 functions = (pci_read8(address, 0x0e) & 0x80) ? 8 : 1;
        for (u8 function = 0; function < functions; function++) {
            address.function = function;
            if (pci_read16(address, 0) == 0xffff)
                continue;
            count++;
            if (callback) {
                PCI_ret info = pci_get_info(address);
                callback(&info, context);
            }
        }
    }
    return count;
}

int pci_enumerate(pci_enumerate_callback_t callback, void *context) {
    int count = 0;
    if (ecam_region_count) {
        for (u32 region = 0; region < ecam_region_count; region++) {
            pci_ecam_region_t *entry = &ecam_regions[region];
            for (u16 bus = entry->start_bus; bus <= entry->end_bus; bus++)
                count += enumerate_bus(entry->segment, (u8)bus,
                                       callback, context);
        }
    } else {
        for (u16 bus = 0; bus < 256; bus++)
            count += enumerate_bus(0, (u8)bus, callback, context);
    }
    return count;
}

int PCI_list(void) {
    return pci_enumerate(0, 0);
}

int pci_find_capability(pci_address_t address, u8 capability_id, u16 *offset) {
    if (!offset || !(pci_read16(address, 0x06) & PCI_STATUS_CAPABILITIES))
        return PCI_STATUS_NOT_FOUND;
    u8 header_type = pci_read8(address, 0x0e) & 0x7f;
    u8 pointer = pci_read8(address, header_type == 2 ? 0x14 : 0x34) & 0xfc;
    for (u8 visited = 0; pointer >= 0x40 && visited < 48; visited++) {
        if (pci_read8(address, pointer) == capability_id) {
            *offset = pointer;
            return PCI_STATUS_SUCCESS;
        }
        pointer = pci_read8(address, pointer + 1) & 0xfc;
    }
    return PCI_STATUS_NOT_FOUND;
}

int pci_find_extended_capability(pci_address_t address, u16 capability_id,
                                 u16 *offset) {
    if (!offset || !find_ecam_region(address.segment, address.bus))
        return PCI_STATUS_UNSUPPORTED;
    u16 pointer = 0x100;
    for (u16 visited = 0; pointer >= 0x100 && pointer < 0x1000 &&
         visited < 960; visited++) {
        u32 header = pci_read32(address, pointer);
        if (header == 0 || header == 0xffffffff)
            break;
        if ((header & 0xffff) == capability_id) {
            *offset = pointer;
            return PCI_STATUS_SUCCESS;
        }
        u16 next = (header >> 20) & 0xffc;
        if (!next || next == pointer)
            break;
        pointer = next;
    }
    return PCI_STATUS_NOT_FOUND;
}

int pci_set_command_bits(pci_address_t address, u16 set, u16 clear) {
    if (pci_read16(address, 0) == 0xffff)
        return PCI_STATUS_NOT_PRESENT;
    u16 command = pci_read16(address, 0x04);
    command = (command | set) & (u16)~clear;
    return pci_write16(address, 0x04, command);
}

int pci_enable_device(pci_address_t address) {
    return pci_set_command_bits(address,
                                PCI_COMMAND_IO_SPACE | PCI_COMMAND_MEMORY_SPACE,
                                0);
}

int pci_enable_bus_mastering(pci_address_t address) {
    return pci_set_command_bits(address, PCI_COMMAND_BUS_MASTER, 0);
}

int pci_disable_bus_mastering(pci_address_t address) {
    return pci_set_command_bits(address, 0, PCI_COMMAND_BUS_MASTER);
}

int pci_read_bar(pci_address_t address, u8 bar_index, int probe_size,
                 pci_bar_t *bar) {
    if (!bar || bar_index >= 6 || (pci_read8(address, 0x0e) & 0x7f) != 0)
        return PCI_STATUS_BAD_BAR;
    u16 offset = 0x10 + (u16)bar_index * 4;
    u32 low = pci_read32(address, offset);
    if (low == 0xffffffff)
        return PCI_STATUS_BAD_BAR;

    bar->address = 0;
    bar->size = 0;
    bar->bar_index = bar_index;
    bar->prefetchable = 0;
    bar->is_64bit = 0;
    bar->type = (low & 1) ? PCI_BAR_IO : PCI_BAR_MEMORY;

    u32 high = 0;
    if (bar->type == PCI_BAR_IO) {
        bar->address = low & ~3u;
    } else {
        u8 memory_type = (low >> 1) & 3;
        bar->prefetchable = (low >> 3) & 1;
        if (memory_type == 2) {
            if (bar_index == 5)
                return PCI_STATUS_BAD_BAR;
            bar->is_64bit = 1;
            high = pci_read32(address, offset + 4);
            bar->address = ((u64)high << 32) | (low & ~0x0fu);
        } else {
            bar->address = low & ~0x0fu;
        }
    }

    if (!probe_size)
        return PCI_STATUS_SUCCESS;

    u16 command = pci_read16(address, 0x04);
    pci_write16(address, 0x04,
                command & ~(PCI_COMMAND_IO_SPACE | PCI_COMMAND_MEMORY_SPACE));
    pci_write32(address, offset, 0xffffffff);
    u32 size_low = pci_read32(address, offset);
    u32 size_high = 0;
    if (bar->is_64bit) {
        pci_write32(address, offset + 4, 0xffffffff);
        size_high = pci_read32(address, offset + 4);
        pci_write32(address, offset + 4, high);
    }
    pci_write32(address, offset, low);
    pci_write16(address, 0x04, command);

    if (bar->is_64bit) {
        u64 mask = ((u64)size_high << 32) | (size_low & ~0x0fu);
        if (mask)
            bar->size = (~mask) + 1;
    } else {
        u32 mask = bar->type == PCI_BAR_IO ? (size_low & ~3u) :
                                             (size_low & ~0x0fu);
        if (mask)
            bar->size = (u32)(~mask + 1u);
    }
    return PCI_STATUS_SUCCESS;
}

PCI_PATH_RET Discover_BootDevice(u64 path_addr, int size) {
    PCI_PATH_RET result = {0};
    result.PCI_status = PCI_STATUS_NOT_PRESENT;
    if (!path_addr || size < (int)sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
        result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
        return result;
    }

    u8 *node_address = (u8 *)path_addr;
    u8 *path_end = node_address + size;
    pci_address_t address = {0, 0, 0, 0};
    while (node_address + sizeof(EFI_DEVICE_PATH_PROTOCOL) <= path_end) {
        EFI_DEVICE_PATH_PROTOCOL *node = (EFI_DEVICE_PATH_PROTOCOL *)node_address;
        u16 length = device_path_node_length(node);
        if (length < sizeof(*node) || node_address + length > path_end) {
            result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
            return result;
        }
        if (node->Type == EFI_DEVICE_PATH_TYPE_END &&
            node->SubType == EFI_DEVICE_PATH_SUBTYPE_END_ENTIRE)
            return result;
        if (node->Type == EFI_DEVICE_PATH_TYPE_HARDWARE &&
            node->SubType == EFI_DEVICE_PATH_SUBTYPE_PCI) {
            if (length < sizeof(PCI_DEVICE_PATH)) {
                result.PCI_status = PCI_STATUS_INVALID_ADDRESS;
                return result;
            }
            if (result.DeviceCount >= PCI_PATH_MAX_DEVICES) {
                result.PCI_status = PCI_STATUS_TOO_MANY_DEVICES;
                return result;
            }
            PCI_DEVICE_PATH *pci_node = (PCI_DEVICE_PATH *)node;
            address.device = pci_node->Device;
            address.function = pci_node->Function;
            PCI_ret device = pci_get_info(address);
            if (device.PCI_status != PCI_STATUS_SUCCESS) {
                result.PCI_status = device.PCI_status;
                return result;
            }
            result.Devices[result.DeviceCount++] = device;
            result.PCI_status = PCI_STATUS_SUCCESS;
            if (device.PCI_Type == 1)
                address.bus = device.header.header1.secondary_bus;
        }
        node_address += length;
    }
    return result;
}

void pci_init(void) {
    spinlock_init(&legacy_config_lock);
    Express_enabled = 0;
    BusCount = 0;
    ecam_region_count = 0;

    ACPI_ret result = ACPI_discovery("MCFG");
    if (result.status == 0) {
        MCFG *mcfg = (MCFG *)result.Address;
        if (mcfg->header.length >= sizeof(MCFG)) {
            u32 available = (mcfg->header.length - sizeof(MCFG)) /
                            sizeof(MCFGEntry);
            MCFGEntry *entries = (MCFGEntry *)((u8 *)mcfg + sizeof(MCFG));
            u64 virtual_base = PCI_BASE;
            for (u32 i = 0; i < available &&
                 ecam_region_count < PCI_MAX_ECAM_REGIONS; i++) {
                if (entries[i].StartBus > entries[i].EndBus)
                    continue;
                u64 buses = (u64)entries[i].EndBus - entries[i].StartBus + 1;
                u64 pages = buses << 8;
                pci_ecam_region_t *region = &ecam_regions[ecam_region_count++];
                region->physical_base = entries[i].BaseAddress;
                region->virtual_base = virtual_base;
                region->segment = entries[i].SegmentGroup;
                region->start_bus = entries[i].StartBus;
                region->end_bus = entries[i].EndBus;
                create_mapping(virtual_base, entries[i].BaseAddress, pages,
                               0x13, KernelPML4);
                flush_pages(virtual_base, pages);
                virtual_base += pages << 12;
                BusCount += (int)buses;
                printf("PCIe ECAM segment %u buses %u-%u at 0x%lx\n",
                       (unsigned int)region->segment,
                       (unsigned int)region->start_bus,
                       (unsigned int)region->end_bus, region->virtual_base);
            }
        }
    }

    Express_enabled = ecam_region_count != 0;
    if (!Express_enabled) {
        BusCount = 256;
        printf("No PCIe ECAM detected; using PCI config ports 0xCF8/0xCFC\n");
    }
    printf("%d PCI devices detected\n", PCI_list());
}
