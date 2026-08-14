#ifndef PCI_h
#define PCI_h

#include "uint_definitions.h"

#define PCI_STATUS_SUCCESS 0
#define PCI_STATUS_NOT_PRESENT 1
#define PCI_STATUS_UNSUPPORTED_HEADER 2
#define PCI_STATUS_INVALID_ADDRESS 3
#define PCI_STATUS_TOO_MANY_DEVICES 4
#define PCI_PATH_MAX_DEVICES 32

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

u32 PCI_read(u8 bus, u8 device, u8 function, u8 offset, u8 size);
PCI_ret PCI_get_info(u8 bus, u8 device, u8 function);
PCI_PATH_RET Discover_BootDevice(u64 path_addr, int size);
int PCI_list(void);
void pci_init(void);

#endif
