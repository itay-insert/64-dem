#ifndef PATH_H
#define PATH_H

#include <stddef.h>
#include "uint_definitions.h"
#include "c_compat.h"

#define EFI_DEVICE_PATH_TYPE_HARDWARE 0x01
#define EFI_DEVICE_PATH_SUBTYPE_PCI 0x01
#define EFI_DEVICE_PATH_TYPE_END 0x7f
#define EFI_DEVICE_PATH_SUBTYPE_END_ENTIRE 0xff

typedef struct __attribute__((packed)) {
    u8 Type;
    u8 SubType;
    u8 Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

typedef struct __attribute__((packed)) {
    EFI_DEVICE_PATH_PROTOCOL Header;
    u8 Function;
    u8 Device;
} PCI_DEVICE_PATH;

KERNEL_STATIC_ASSERT(sizeof(EFI_DEVICE_PATH_PROTOCOL) == 4,
               "EFI device-path header layout changed");
KERNEL_STATIC_ASSERT(offsetof(EFI_DEVICE_PATH_PROTOCOL, Type) == 0,
               "EFI device-path Type offset changed");
KERNEL_STATIC_ASSERT(offsetof(EFI_DEVICE_PATH_PROTOCOL, SubType) == 1,
               "EFI device-path SubType offset changed");
KERNEL_STATIC_ASSERT(offsetof(EFI_DEVICE_PATH_PROTOCOL, Length) == 2,
               "EFI device-path Length offset changed");

KERNEL_STATIC_ASSERT(sizeof(PCI_DEVICE_PATH) == 6,
               "PCI device-path node layout changed");
KERNEL_STATIC_ASSERT(offsetof(PCI_DEVICE_PATH, Function) == 4,
               "PCI device-path Function offset changed");
KERNEL_STATIC_ASSERT(offsetof(PCI_DEVICE_PATH, Device) == 5,
               "PCI device-path Device offset changed");

static inline u16 device_path_node_length(
    const EFI_DEVICE_PATH_PROTOCOL *node
) {
    return (u16)node->Length[0] | ((u16)node->Length[1] << 8);
}

#endif
