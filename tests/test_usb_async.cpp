#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "drivers/usb/usb.hpp"

static unsigned sleeps = 0;
class FakeTimer : public TimerSource {
public:
    u64 ticks = 0;
    u64 read() override { return ticks; }
    u64 read_freq() override { return 1000; }
};
void wait_ms(TimerSource &timer, u64 ms) {
    ++sleeps;
    static_cast<FakeTimer &>(timer).ticks += ms;
}
// The real discovery/PCI layer is unnecessary for exercising the shared API.
void usb_controller::scan_ports(TimerSource &) {}

class FakeController : public usb_controller {
    unsigned steps = 0;
    unsigned char dma[8] = {};
protected:
    usb_transfer_result start_transfer(u32 handle, void *buffer, u32 length,
                                      TimerSource &) override {
        if (length > sizeof(dma)) return transfer_result(INVALID_LENGTH, handle);
        steps = 0;
        async.in = handle == 0;
        async.dma_buffer = dma;
        if (!async.in && length) {
            if (!buffer) return transfer_result(FAILED, handle);
            memcpy(dma, buffer, length);
        }
        return transfer_result(TRANSFER_PENDING, handle);
    }
    void advance_transfer(TimerSource &timer) override {
        if (async.cancel || transfer_expired(timer)) {
            // Model a cleanup transition that itself needs another poll.
            if (++steps < 2) return;
            transfer_complete(async.cancel ? TRANSFER_CANCELLED : TIMEOUT);
        } else if (++steps == 3) {
            if (async.in)
                for (u32 i = 0; i < async.length; ++i) dma[i] = (unsigned char)(i + 1);
            async.result.actual_length = async.length;
            transfer_complete(TRANSFER_SUCCESS);
        }
    }
public:
    FakeController() { controller = {}; controller_type = 4; controller.index = 2; }
    int controller_init(TimerSource &) override { return 0; }
    int enumrate_ports() override { return 0; }
    int reset_port(u8, TimerSource &) override { return 0; }
    int disable_port(u8, TimerSource &) override { return 0; }
    usb_device_descriptor identify_device(u8, TimerSource &) override { return {}; }
    usb_device set_address(usb_device_descriptor &, TimerSource &, u8) override { return {}; }
    void *usb_send_packet(usb_setup_packet, u32, u64, u8, TimerSource &, u8) override { return nullptr; }
    int configure_endpoints(usb_device, u16, u32, u8, TimerSource &) override { return 0; }
    const unsigned char *packet() const { return dma; }
};

int main() {
    FakeController hc;
    FakeTimer timer;
    unsigned char buffer[8] = {};
    auto submitted = hc.usb_submit_transfer(0, nullptr, 8, timer);
    assert(submitted.status_code == TRANSFER_PENDING && submitted.request_id);
    assert(submitted.controller_type == 4 && submitted.controller_index == 2);
    u64 id = submitted.request_id;
    assert(hc.usb_submit_transfer(0, nullptr, 8, timer).status_code == TRANSFER_BUSY);
    assert(hc.usb_read_transfer(id + 1, buffer, 8, timer).status_code == INVALID_REQUEST);
    for (unsigned i = 0; i < 2; ++i) {
        assert(hc.usb_read_transfer(id, buffer, 8, timer).status_code == TRANSFER_PENDING);
        assert(buffer[0] == 0);
    }
    auto short_buffer = hc.usb_read_transfer(id, buffer, 1, timer);
    assert(short_buffer.status_code == INVALID_LENGTH && short_buffer.actual_length == 8);
    assert(hc.usb_submit_transfer(0, nullptr, 8, timer).status_code == TRANSFER_BUSY);
    auto result = hc.usb_read_transfer(id, buffer, 8, timer);
    assert(result.status_code == TRANSFER_SUCCESS && result.actual_length == 8);
    for (unsigned i = 0; i < 8; ++i) assert(buffer[i] == i + 1);
    assert(hc.usb_read_transfer(id, buffer, 8, timer).status_code == INVALID_REQUEST);
    assert(sleeps == 0); // Submit/read never call the blocking timer helper.

    auto out = hc.usb_submit_transfer(1, buffer, 8, timer);
    assert(out.request_id != id);
    memset(buffer, 0, sizeof(buffer));
    assert(hc.packet()[0] == 1); // Caller may reuse its OUT buffer immediately.
    hc.usb_cancel_transfer(out.request_id, timer);
    assert(hc.usb_read_transfer(out.request_id, nullptr, 0, timer).status_code == TRANSFER_PENDING);
    assert(hc.usb_read_transfer(out.request_id, nullptr, 0, timer).status_code == TRANSFER_CANCELLED);

    timer.ticks = 0xfffffe;
    auto timed = hc.usb_submit_transfer(0, nullptr, 8, timer, 3);
    timer.ticks = 2; // Wrapping timer: four elapsed ticks.
    assert(hc.usb_read_transfer(timed.request_id, buffer, 8, timer).status_code == TRANSFER_PENDING);
    assert(hc.usb_read_transfer(timed.request_id, buffer, 8, timer).status_code == TIMEOUT);
    assert(sleeps == 0);

    auto failed = hc.usb_submit_transfer(0, nullptr, 9, timer);
    assert(failed.status_code == INVALID_LENGTH && !failed.request_id);
    auto blocking = hc.usb_transfer(0, buffer, 8, timer, 100);
    assert(blocking.status_code == TRANSFER_SUCCESS && sleeps == 2);
    assert(hc.usb_cancel_transfer(id, timer).status_code == INVALID_REQUEST);
    puts("USB asynchronous lifecycle tests passed");
}
