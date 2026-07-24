#ifndef PCI_NAMES_H
#define PCI_NAMES_H

#include "uint_definitions.h"

typedef struct {
    u16 vendor_id;
    u32 name_offset;
} PCI_VENDOR_NAME;

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u32 name_offset;
} PCI_DEVICE_NAME;

extern const char pci_name_data[];
extern const PCI_VENDOR_NAME pci_vendor_names[];
extern const u64 pci_vendor_name_count;
extern const PCI_DEVICE_NAME pci_device_names[];
extern const u64 pci_device_name_count;

const char *PCI_vendor_name(u16 vendor_id);
const char *PCI_device_name(u16 vendor_id, u16 device_id);

#endif
