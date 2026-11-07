#include "drivers/keyboard/hid.hpp"
#include "drivers/usb/uhci.hpp"
#include "drivers/usb/ohci.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/xhci.hpp"
#include "x86-64/memory/memory.h"
#include "drivers/display/vga.h"

extern u64 keyboard_usb_address, usb_addr, timeradd;
extern bool keyboard_usb_found;
extern volatile u64 keyboard_irq_count;

static void init_error(const char *reason) {
    printf("USB keyboard: initialization failed: %s\n", reason);
}

void HIDK::init() {
    if (ready) return;
    if (!keyboard_usb_found) {
        printf("USB keyboard: no boot-keyboard interrupt-IN endpoint discovered\n");
        return;
    }
    if (!usb_addr || !timeradd) return init_error("USB/timer globals unavailable");
    auto *usb = reinterpret_cast<GlobalUSB *>(usb_addr);
    auto *timer = reinterpret_cast<Timer *>(timeradd);
    if (!timer->Src) return init_error("timer source unavailable");
    const u8 type = keyboard_usb_address >> 32; // dispatch usb address to type and index
    const u8 index = keyboard_usb_address >> 24;
    switch (type) {  // get controller from type and index
    case 0: if (index < usb->uhci_count) controller = &usb->uhci_controllers[index]; break;
    case 1: if (index < usb->ohci_count) controller = &usb->ohci_controllers[index]; break;
    case 2: if (index < usb->ehci_count) controller = &usb->ehci_controllers[index]; break;
    case 3: if (index < usb->xhci_count) controller = &usb->xhci_controllers[index]; break;
    }
    if (!controller) return init_error("invalid controller selection");
    const u8 port = keyboard_usb_address >> 16; // extract usb address to port, iface and ep
    const u8 iface = keyboard_usb_address >> 8;
    const u8 ep = keyboard_usb_address;
    if (!controller->ports || port >= controller->ports_count ||
        !controller->ports[port].connected || !controller->ports[port].device)
        return init_error("selected port has no enumerated device");
    usb_device &device = *controller->ports[port].device;
    if (!device.discovered || !device.interfaces || iface >= device.usb_interface_count)
        return init_error("selected interface unavailable");
    const usb_interface &interface = device.interfaces[iface];
    if (interface.descriptor.bInterfaceClass != 3 ||  // validate the interface
        interface.descriptor.bInterfaceSubClass != 1 ||
        interface.descriptor.bInterfaceProtocol != 1 || !interface.endpoints ||
        ep >= interface.endpoint_count || interface.endpoints[ep].transfer_type != 3 ||
        !(interface.endpoints[ep].address & IN))
        return init_error("selected interface/endpoint is not a boot keyboard");


    printf("USB keyboard: %s controller %u, port %u, interface %u, endpoint 0x%b, VID=0x%w PID=0x%w\n",
        controller->vendor, (u32)index, (u32)device.port_number,
        (u32)interface.descriptor.bInterfaceNumber, interface.endpoints[ep].address,
        device.descriptor.idVendor, device.descriptor.idProduct);
    usb_setup_packet packet = {0x21, 0x0b, 0,
        interface.descriptor.bInterfaceNumber, 0};  // send SET_PROTOCOL usb packet

    if (!controller->usb_send_packet(packet, device.descriptor.bMaxPacketSize0,
            0, device.address, *timer->Src, device.port_number))
        return init_error("SET_PROTOCOL control transfer failed");
    const u32 handle = u32(port) << 16 | u32(iface) << 8 | ep;
    ready = controller->usb_register_interrupt(handle, sizeof(hid_keyboard_report), completion, this);
    if (!ready) return init_error("interrupt subscription rejected");
    if (ready) {
        printf("USB keyboard: subscribed; %s; waiting for reports\n",
            controller->usb_irq_ready() ? "MSI/MSI-X configured" : "polling");
        vendor[0] = 'U'; vendor[1] = 'S'; vendor[2] = 'B'; vendor[3] = 0;
        controller->usb_service_interrupts(*timer->Src);
    }
}

void HIDK::completion(void *context, const usb_transfer_result &result, const u8 *data) {
    auto &keyboard = *static_cast<HIDK *>(context);
    if (result.status_code != TRANSFER_SUCCESS) {
        printf("USB keyboard: transfer failed, status=%d hardware=0x%ux length=%u\n",
            result.status_code, result.hardware_status, result.actual_length);
        keyboard.process_report({}); // Release held keys on terminal failure.
        keyboard.ready = false;
        return;
    }
    if (result.actual_length != sizeof(hid_keyboard_report)) {
        if (!keyboard.warned_short_report) {
            printf("USB keyboard: ignoring report of %u bytes (expected 8)\n", result.actual_length);
            keyboard.warned_short_report = true;
        }
        return;
    }
    if (!keyboard.received_report) {
        printf("USB keyboard: first 8-byte input report received\n");
        keyboard.received_report = true;
    }
    hid_keyboard_report incoming;
    memcpy(&incoming, data, sizeof(incoming));
    keyboard.process_report(incoming);
}
