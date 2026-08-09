#include "pci_names.h"

#include "pci_names_data.inc"

const char *PCI_vendor_name(u16 vendor_id) {
    u64 low = 0;
    u64 high = pci_vendor_name_count;

    while (low < high) {
        u64 middle = low + ((high - low) >> 1);
        u16 candidate = pci_vendor_names[middle].vendor_id;

        if (candidate < vendor_id)
            low = middle + 1;
        else if (candidate > vendor_id)
            high = middle;
        else
            return pci_name_data + pci_vendor_names[middle].name_offset;
    }

    return "Unknown PCI vendor";
}

const char *PCI_device_name(u16 vendor_id, u16 device_id) {
    u32 requested_id = ((u32)vendor_id << 16) | device_id;
    u64 low = 0;
    u64 high = pci_device_name_count;

    while (low < high) {
        u64 middle = low + ((high - low) >> 1);
        u32 candidate_id =
            ((u32)pci_device_names[middle].vendor_id << 16) |
            pci_device_names[middle].device_id;

        if (candidate_id < requested_id)
            low = middle + 1;
        else if (candidate_id > requested_id)
            high = middle;
        else
            return pci_name_data + pci_device_names[middle].name_offset;
    }

    return "Unknown PCI device";
}
