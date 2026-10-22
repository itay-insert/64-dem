#include "uint_definitions.h"
#include "x86-64/io.h"
#include "x86-64/paging.h"
#include "x86-64/memory/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"

typedef struct {
    u32 revision;             // 0x00 HcRevision
    u32 control;              // 0x04 HcControl
    u32 command_status;       // 0x08 HcCommandStatus
    u32 interrupt_status;    // 0x0C HcInterruptStatus
    u32 interrupt_enable;    // 0x10 HcInterruptEnable
    u32 interrupt_disable;   // 0x14 HcInterruptDisable
    u32 hcca;                 // 0x18 HcHCCA
    u32 periodic_current_ed;  // 0x1C HcPeriodCurrentED
    u32 control_head_ed;      // 0x20 HcControlHeadED
    u32 control_current_ed;   // 0x24 HcControlCurrentED
    u32 bulk_head_ed;         // 0x28 HcBulkHeadED
    u32 bulk_current_ed;      // 0x2C HcBulkCurrentED
    u32 done_head;            // 0x30 HcDoneHead
    u32 fm_interval;          // 0x34 HcFmInterval
    u32 fm_remaining;         // 0x38 HcFmRemaining
    u32 fm_number;            // 0x3C HcFmNumber
    u32 periodic_start;       // 0x40 HcPeriodicStart
    u32 ls_threshold;         // 0x44 HcLSThreshold
    u32 rh_descriptor_a;      // 0x48 HcRhDescriptorA
    u32 rh_descriptor_b;      // 0x4C HcRhDescriptorB
    u32 rh_status;            // 0x50 HcRhStatus
    u32 rh_port_status[15];   // 0x54 HcRhPortStatus[0..14]
} ohci_registers_t;

typedef struct {
    bool found;
    pci_address_t pci_address;
    pci_bar_t bar;
    u64 io_base;
} ohci_controller_t;

ohci_controller_t ohci_controller = {0};

static void ohci_find_callback(const PCI_ret *device, void *context) {
    ohci_controller_t *controller =
        static_cast<ohci_controller_t *>(context);

    if (controller->found || 
    device->PCI_status != PCI_STATUS_SUCCESS) 
        return;
    
    if (device->common.class_code != 0x0C ||
        device->common.subclass != 0x03 ||
        device->common.prog_if != 0x10)
        return;

    controller->pci_address = (pci_address_t) {
        .segment = device->Segment,
        .bus = device->Bus,
        .device = device->Device,
        .function = device->Function
    };

    controller->found = true;
    return;
}


bool ohci_discover(ohci_controller_t *controller) {
    *controller = {};

    pci_enumerate(ohci_find_callback, controller);

    if (!controller->found) {
        printf("OHCI: controller not found\n");
        return false;
    }

    printf("OHCI: controller found at %u:%u:%u.%u\n", 
        (unsigned int)controller->pci_address.segment, 
        (unsigned int)controller->pci_address.bus, 
        (unsigned int)controller->pci_address.device, 
        (unsigned int)controller->pci_address.function);
    return true;
}


int OHCI_init(TimerSource &timer) {
    ohci_controller_t *controller = &ohci_controller;

    if (!ohci_discover(controller))   // discover OHCI
        return 1;

    if (pci_enable_device(controller->pci_address)
     != PCI_STATUS_SUCCESS) { // enable the device on PCI
        printf("OHCI: failed to enable pci device\n");
        return 1;
    }

    if (pci_enable_bus_mastering(controller->pci_address) != PCI_STATUS_SUCCESS) { // enable bus mastering
        printf("OHCI: failed to enable device bus mastering\n");
        return 1;
    }

    
}