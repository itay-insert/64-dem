#pragma once
#include "drivers/pci/pci.h"
#include "x86-64/irq.h"
KERNEL_EXTERN_C_BEGIN
enum pci_irq_mode { PCI_IRQ_POLLING, PCI_IRQ_MSI, PCI_IRQ_MSIX };
typedef struct {
    pci_address_t address;
    enum pci_irq_mode mode;
    u8 vector;
    u16 capability;
    volatile u32 *table;
} pci_irq_route;
/* Owns the function's interrupt configuration; no guessed legacy INTx route.
 * Sources must remain masked until success. Target is an 8-bit xAPIC ID.
 * Call during boot, before the function has outstanding DMA requests. */
int pci_irq_setup(pci_address_t address, u8 destination, device_irq_handler handler,
                  void *context, pci_irq_route *route);
int pci_irq_disable(pci_irq_route *route);
KERNEL_EXTERN_C_END
