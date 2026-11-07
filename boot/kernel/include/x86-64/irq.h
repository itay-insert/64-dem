#pragma once
#include "uint_definitions.h"
#include "c_compat.h"
#define DEVICE_IRQ_FIRST 0x50
#define DEVICE_IRQ_COUNT 48
KERNEL_EXTERN_C_BEGIN
typedef void (*device_irq_handler)(void *context);
/* Boot-CPU registration. Returns zero on exhaustion. Handler and all its
 * callees must be IRQ-safe and preserve SIMD/FPU state (e.g. -mgeneral-regs-only).
 * Mask/disable the device source before releasing its vector. */
u8 device_irq_allocate(device_irq_handler handler, void *context);
void device_irq_release(u8 vector);
void device_irq_dispatch(u64 vector);
/* Platform mapping used by PCI MSI-X. */
void *pci_irq_map_mmio(u64 physical, u64 length);
KERNEL_EXTERN_C_END
