#pragma once
// QEMU-only integration probe. Included by the temporary test kernel, never by
// the production kernel. BOT commands here validate transport concurrency; this
// is not the mass-storage abstraction.
#include "drivers/usb/usb.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/kmalloc.h"

extern volatile u64 keyboard_irq_count;
extern u64 keyboard_usb_address;
extern bool keyboard_usb_found;

static void usb_concurrency_probe(GlobalUSB &USB, TimerSource &timer) {
    auto found = USB.find_interface(8, 6, true, 0x50);
    if (!found.interface_count) {
        printf("CONCURRENCY FAIL no storage interface\n");
    } else {
        auto &iface = found.interfaces[0];
        auto *hc = iface.controller;
        if (!keyboard_usb_found || !hc->usb_interrupt_registered() ||
            u8(keyboard_usb_address >> 32) + 1 != hc->controller_type ||
            u8(keyboard_usb_address >> 24) != hc->controller.index) {
            printf("CONCURRENCY FAIL keyboard is not on storage controller\n");
            return;
        }
        auto &dev = *hc->ports[iface.port_index].device;
        const auto &intf = dev.interfaces[iface.interface_index];
        const u32 in_handle = u32(iface.port_index) << 16 | u32(iface.interface_index) << 8 | iface.endpoint_bulk_in;
        const u32 out_handle = u32(iface.port_index) << 16 | u32(iface.interface_index) << 8 | iface.endpoint_bulk_out;
        const u32 mps = intf.endpoints[iface.endpoint_bulk_in].max_packet_size;
        printf("CONCURRENCY storage controller=%u index=%u mps=%u\n", u32(hc->controller_type), u32(hc->controller.index), mps);
        unsigned tag = 0;
        auto command = [&](u8 opcode, u8 *data, u32 length, u32 lba) -> int {
            struct __attribute__((packed)) CBW {
                u32 signature, tag, length;
                u8 flags, lun, cdb_length, cdb[16];
            } cbw = {};
            cbw.signature = 0x43425355;
            cbw.tag = ++tag;
            cbw.length = length;
            cbw.flags = 0x80;
            cbw.cdb_length = opcode == 0x28 ? 10 : 6;
            cbw.cdb[0] = opcode;
            if (opcode == 0x28) {
                cbw.cdb[2] = lba >> 24;
                cbw.cdb[3] = lba >> 16;
                cbw.cdb[4] = lba >> 8;
                cbw.cdb[5] = lba;
                cbw.cdb[8] = 1;
            } else cbw.cdb[4] = length;
            auto r = hc->usb_transfer(out_handle, &cbw, sizeof(cbw), timer, 1000);
            if (r.status_code != TRANSFER_SUCCESS || r.actual_length != sizeof(cbw)) {
                printf("CONCURRENCY FAIL cbw status=%d len=%u\n", r.status_code, r.actual_length); return -1;
            }
            // BOT requires the CBW before its IN stage. Submit that stage
            // asynchronously alongside the keyboard and collect it later.
            const u32 first_length = length ? (length < mps ? length : mps) : 13;
            auto first = hc->usb_submit_transfer(in_handle, nullptr, first_length, timer, 1000);
            if (first.status_code != TRANSFER_PENDING) {
                printf("CONCURRENCY FAIL prearm status=%d\n", first.status_code); return -1;
            }
            auto collect = [&](void *destination, u32 capacity) -> usb_transfer_result {
                usb_transfer_result result;
                do {
                    result = hc->usb_read_transfer(first.request_id, destination, capacity, timer);
                    hc->usb_service_interrupts(timer);
                    if (result.status_code == TRANSFER_PENDING) wait_ms(timer, 1);
                } while (result.status_code == TRANSFER_PENDING);
                return result;
            };
            if ((tag % 32) == 0) {
                usb_setup_packet setup = {0x80, 8, 0, 0, 1};
                auto *configuration = (u8 *)hc->usb_send_packet(setup,
                    dev.descriptor.bMaxPacketSize0 == 9 ? 512 : dev.descriptor.bMaxPacketSize0,
                    1, dev.address, timer, dev.port_number);
                if (!configuration || *configuration != dev.config.bConfigurationValue) {
                    printf("CONCURRENCY FAIL control with bulk IN outstanding\n"); return -1;
                }
                kfree(configuration);
            }
            u32 start = 0;
            if (length) {
                r = collect(data, first_length);
                if (r.status_code != TRANSFER_SUCCESS || r.actual_length != first_length) {
                    printf("CONCURRENCY FAIL prearmed data status=%d len=%u\n", r.status_code, r.actual_length); return -1;
                }
                start = r.actual_length;
            }
            for (u32 offset = start; offset < length;) {
                const u32 n = length - offset < mps ? length - offset : mps;
                r = hc->usb_transfer(in_handle, data + offset, n, timer, 1000);
                if (r.status_code != TRANSFER_SUCCESS) {
                    printf("CONCURRENCY FAIL data status=%d len=%u\n", r.status_code, r.actual_length); return -1;
                }
                offset += r.actual_length;
                if (r.actual_length < n) break;
            }
            struct __attribute__((packed)) CSW { u32 signature, tag, residue; u8 status; } csw = {};
            r = length ? hc->usb_transfer(in_handle, &csw, sizeof(csw), timer, 1000)
                       : collect(&csw, sizeof(csw));
            if (r.status_code != TRANSFER_SUCCESS || r.actual_length != sizeof(csw) ||
                csw.signature != 0x53425355 || csw.tag != cbw.tag) {
                printf("CONCURRENCY FAIL csw status=%d len=%u sig=%ux\n", r.status_code, r.actual_length, csw.signature); return -1;
            }
            return csw.status;
        };
        u8 sector[512] = {};
        bool good = command(0x12, sector, 36, 0) == 0;
        for (unsigned i = 0; good && i < 3; ++i) {
            int status = command(0, sector, 0, 0);
            if (status < 0) { good = false; break; }
            if (status == 0) break;
            good = command(3, sector, 18, 0) >= 0;
        }
        auto control = [&]() -> bool {
            usb_setup_packet packet = {0x80, 8, 0, 0, 1};
            auto *value = (u8 *)hc->usb_send_packet(packet, dev.descriptor.bMaxPacketSize0 == 9 ? 512 : dev.descriptor.bMaxPacketSize0, 1,
                                                  dev.address, timer, dev.port_number);
            const bool ok = value && *value == dev.config.bConfigurationValue;
            if (value) kfree(value);
            if (!ok) printf("CONCURRENCY FAIL control with keyboard pending\n");
            return ok;
        };
        good = good && control();
        const u64 initial_keys = keyboard_irq_count;
        for (unsigned i = 0; good && i < 144; ++i) {
            if (i == 16) printf("CONCURRENCY TYPING start idle_reads=16 keys=%lu\n", keyboard_irq_count - initial_keys);
            if (i && i % 32 == 0) good = control();
            const u32 lba = i % 64;
            good = good && command(0x28, sector, 512, lba) == 0;
            for (unsigned j = 0; good && j < 512; ++j)
                if (sector[j] != u8((lba * 17 + j * 13) & 255)) {
                    printf("CONCURRENCY FAIL corrupt sector=%u byte=%u actual=%u\n", lba, j, u32(sector[j]));
                    good = false;
                }
            if (good && i % 16 == 15) printf("CONCURRENCY progress=%u keys=%lu\n", i + 1, keyboard_irq_count - initial_keys);
            wait_ms(timer, 10);
        }
        printf("CONCURRENCY %s reads=144 keys=%lu irq=%lu\n", good && keyboard_irq_count != initial_keys ? "PASS" : "FAIL",
               keyboard_irq_count - initial_keys, hc->usb_irq_count());
    }
}
