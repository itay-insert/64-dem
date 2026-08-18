#ifndef PCI_H
#define PCI_H

#include "uint_definitions.h"

#define PCI_STATUS_SUCCESS              0
#define PCI_STATUS_NOT_PRESENT          1
#define PCI_STATUS_UNSUPPORTED_HEADER   2
#define PCI_STATUS_INVALID_ADDRESS      3
#define PCI_STATUS_TOO_MANY_DEVICES     4
#define PCI_STATUS_UNSUPPORTED          5
#define PCI_STATUS_NOT_FOUND            6
#define PCI_STATUS_BAD_BAR              7

#define PCI_PATH_MAX_DEVICES 32

#define PCI_COMMAND_IO_SPACE        (1u << 0)
#define PCI_COMMAND_MEMORY_SPACE    (1u << 1)
#define PCI_COMMAND_BUS_MASTER      (1u << 2)
#define PCI_COMMAND_INTX_DISABLE    (1u << 10)
#define PCI_STATUS_CAPABILITIES     (1u << 4)

#define PCI_BAR_MEMORY 0
#define PCI_BAR_IO     1

typedef struct {
    u16 segment;
    u8 bus;
    u8 device;
    u8 function;
} pci_address_t;

typedef struct {
    u64 address;
    u64 size;
    u8 type;
    u8 prefetchable;
    u8 is_64bit;
    u8 bar_index;
} pci_bar_t;

typedef struct {
    u16 command;
    u16 status;
    u8 revision_id;
    u8 prog_if;
    u8 subclass;
    u8 class_code;
    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;
    u8 bist;
} __attribute__((packed)) PCICommonHeader;

typedef struct {
    u32 bar[6];
    u32 cardbus_cis_ptr;
    u16 subsystem_vendor_id;
    u16 subsystem_id;
    u32 expansion_rom;
    u8 capabilities_ptr;
    u8 reserved1[3];
    u32 reserved2;
    u8 interrupt_line;
    u8 interrupt_pin;
    u8 min_grant;
    u8 max_latency;
} __attribute__((packed)) PCIHeaderType0;

typedef struct {
    u32 bar[2];
    u8 primary_bus;
    u8 secondary_bus;
    u8 subordinate_bus;
    u8 secondary_latency;
    u8 io_base;
    u8 io_limit;
    u16 secondary_status;
    u16 memory_base;
    u16 memory_limit;
    u16 prefetch_base;
    u16 prefetch_limit;
    u32 prefetch_base_upper32;
    u32 prefetch_limit_upper32;
    u16 io_base_upper16;
    u16 io_limit_upper16;
    u8 capabilities_ptr;
    u8 reserved[3];
    u32 expansion_rom;
    u8 interrupt_line;
    u8 interrupt_pin;
    u16 bridge_control;
} __attribute__((packed)) PCIHeaderType1;

typedef struct {
    u32 cardbus_socket_base;
    u8 capabilities_ptr;
    u8 reserved;
    u16 secondary_status;
    u8 pci_bus;
    u8 cardbus_bus;
    u8 subordinate_bus;
    u8 cardbus_latency;
    u32 memory_base0;
    u32 memory_limit0;
    u32 memory_base1;
    u32 memory_limit1;
    u32 io_base0;
    u32 io_limit0;
    u32 io_base1;
    u32 io_limit1;
    u8 interrupt_line;
    u8 interrupt_pin;
    u16 bridge_control;
    u16 subsystem_vendor_id;
    u16 subsystem_device_id;
    u32 legacy_base;
} __attribute__((packed)) PCIHeaderType2;

typedef struct {
    int PCI_status;
    int PCI_Type;
    u16 Segment;
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

typedef struct {
    int PCI_status;
    u32 DeviceCount;
    PCI_ret Devices[PCI_PATH_MAX_DEVICES];
} PCI_PATH_RET;

typedef void (*pci_enumerate_callback_t)(const PCI_ret *device, void *context);

/* Segment-aware configuration access used by new drivers. */
int pci_config_read(pci_address_t address, u16 offset, u8 size, u32 *value);
int pci_config_write(pci_address_t address, u16 offset, u8 size, u32 value);
u8 pci_read8(pci_address_t address, u16 offset);
u16 pci_read16(pci_address_t address, u16 offset);
u32 pci_read32(pci_address_t address, u16 offset);
int pci_write8(pci_address_t address, u16 offset, u8 value);
int pci_write16(pci_address_t address, u16 offset, u16 value);
int pci_write32(pci_address_t address, u16 offset, u32 value);

/* Compatibility accessors address PCI segment zero. */
u32 PCI_read(u8 bus, u8 device, u8 function, u16 offset, u8 size);
void PCI_write(u8 bus, u8 device, u8 function, u16 offset, u8 size, u32 value);

PCI_ret pci_get_info(pci_address_t address);
PCI_ret PCI_get_info(u8 bus, u8 device, u8 function);
int pci_enumerate(pci_enumerate_callback_t callback, void *context);

int pci_find_capability(pci_address_t address, u8 capability_id, u16 *offset);
int pci_find_extended_capability(pci_address_t address, u16 capability_id,
                                 u16 *offset);
int pci_set_command_bits(pci_address_t address, u16 set, u16 clear);
int pci_enable_device(pci_address_t address);
int pci_enable_bus_mastering(pci_address_t address);
int pci_disable_bus_mastering(pci_address_t address);
int pci_read_bar(pci_address_t address, u8 bar_index, int probe_size,
                 pci_bar_t *bar);

PCI_PATH_RET Discover_BootDevice(u64 path_addr, int size);
int PCI_list(void);
void pci_init(void);

#endif
