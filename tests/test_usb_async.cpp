#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "drivers/usb/usb.hpp"

static unsigned sleeps = 0;
static unsigned irq_begins = 0, irq_ends = 0;
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
void usb_controller::usb_begin_irq_service() { ++irq_begins; }
void usb_controller::usb_end_irq_service() { ++irq_ends; }

class FakeController : public usb_controller {
    unsigned steps[request_capacity] = {};
    unsigned char dma[request_capacity][8] = {};
protected:
    usb_transfer_result start_transfer(usb_async_request &async, u32 handle, void *buffer, u32 length,
                                      TimerSource &) override {
        const unsigned slot = &async - requests;
        if (length > sizeof(dma[slot])) return transfer_result(INVALID_LENGTH, handle);
        steps[slot] = 0;
        async.in = (handle & 1u) == 0;
        async.dma_buffer = dma[slot];
        if (!async.in && length) {
            if (!buffer) return transfer_result(FAILED, handle);
            memcpy(dma[slot], buffer, length);
        }
        return transfer_result(TRANSFER_PENDING, handle);
    }
    void advance_transfer(usb_async_request &async, TimerSource &timer) override {
        const unsigned slot = &async - requests;
        if (async.cancel || transfer_expired(async, timer)) {
            // Model a cleanup transition that itself needs another poll.
            if (++steps[slot] < 2) return;
            transfer_complete(async, async.cancel ? TRANSFER_CANCELLED : TIMEOUT);
        } else if (hold_keyboard && async.result.endpoint_handle == 0 && !keyboard_input) {
            return; // Model an interrupt-IN endpoint that NAKs until a key arrives.
        } else if (++steps[slot] >= (async.result.endpoint_handle == 0 ? 3u : completion_steps)) {
            if (hold_keyboard && async.result.endpoint_handle == 0) keyboard_input = false;
            if (async.in)
                for (u32 i = 0; i < async.length; ++i) dma[slot][i] = (unsigned char)(i + 1);
            async.result.actual_length = async.length;
            transfer_complete(async, TRANSFER_SUCCESS);
        }
    }
public:
    bool hold_keyboard = false;
    bool keyboard_input = false;
    unsigned completion_steps = 3;
    FakeController() { controller = {}; controller_type = 4; controller.index = 2; }
    int controller_init(TimerSource &) override { return 0; }
    int enumrate_ports() override { return 0; }
    int reset_port(u8, TimerSource &) override { return 0; }
    int disable_port(u8, TimerSource &) override { return 0; }
    usb_device_descriptor identify_device(u8, TimerSource &) override { return {}; }
    usb_device set_address(usb_device_descriptor &, TimerSource &, u8) override { return {}; }
    void *usb_send_packet(usb_setup_packet, u32, u64, u8, TimerSource &, u8) override { return nullptr; }
    int configure_endpoints(usb_device, u16, u32, u8, TimerSource &) override { return 0; }
    const unsigned char *packet() const { return dma[0]; }
    const unsigned char *packet(u64 id) {
        auto *request = find_request(id);
        return request ? (const unsigned char *)request->dma_buffer : nullptr;
    }
    bool pending(u64 id) {
        auto *request = find_request(id);
        return request && !request->completed;
    }
    bool pending_handle(u32 handle) const {
        for (const auto &request : requests)
            if (request.active && !request.completed && request.result.endpoint_handle == handle)
                return true;
        return false;
    }
    unsigned capacity() const { return request_capacity; }
};

struct CallbackState {
    FakeController *controller;
    FakeTimer *timer;
    unsigned calls = 0;
    bool detach = false;
    bool saw_pending_bulk = false;
};
static void completed(void *context, const usb_transfer_result &result, const u8 *data) {
    auto &state = *static_cast<CallbackState *>(context);
    assert(result.status_code == TRANSFER_SUCCESS && result.actual_length == 8);
    for (unsigned i = 0; i < 8; ++i) assert(data[i] == i + 1);
    ++state.calls;
    state.saw_pending_bulk |= state.controller->pending_handle(2);
    // Recursive service must not collect/rearm twice.
    state.controller->usb_service_interrupts(*state.timer);
    if (state.detach) state.controller->usb_unregister_interrupt(*state.timer);
}

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
    usb_endpoint endpoint = {};
    endpoint.address = 0x81;
    endpoint.transfer_type = 3;
    endpoint.max_packet_size = 8;
    usb_interface interface = {};
    interface.endpoints = &endpoint;
    interface.endpoint_count = 1;
    usb_device device = {};
    device.discovered = true;
    device.interfaces = &interface;
    device.usb_interface_count = 1;
    usb_port_descriptor port = {};
    port.connected = true;
    port.device = &device;
    hc.ports = &port;
    hc.ports_count = 1;
    CallbackState state;
    state.controller = &hc;
    state.timer = &timer;
    assert(!hc.usb_register_interrupt(0, 0, completed, &state));
    assert(!hc.usb_register_interrupt(1, 8, completed, &state));
    assert(!hc.usb_register_interrupt(0, 9, completed, &state));
    endpoint.address = 1;
    assert(!hc.usb_register_interrupt(0, 8, completed, &state));
    endpoint.address = 0x81;
    assert(hc.usb_register_interrupt(0, 8, completed, &state));
    assert(!hc.usb_register_interrupt(0, 8, completed, &state));
    // Subscription waits for a request owned by another caller.
    auto other = hc.usb_submit_transfer(0, nullptr, 8, timer);
    hc.usb_service_interrupts(timer);
    assert(state.calls == 0);
    hc.usb_cancel_transfer(other.request_id, timer);
    hc.usb_read_transfer(other.request_id, buffer, 8, timer);
    hc.usb_read_transfer(other.request_id, buffer, 8, timer);
    for (unsigned i = 0; i < 4; ++i) hc.usb_service_interrupts(timer);
    assert(state.calls == 1 && hc.usb_transfer_pending()); // Automatically rearmed.
    hc.usb_unregister_interrupt(timer);
    assert(hc.usb_interrupt_registered()); // Cancellation still owns DMA.
    assert(!hc.usb_register_interrupt(0, 8, completed, &state));
    hc.usb_service_interrupts(timer);
    hc.usb_service_interrupts(timer);
    assert(state.calls == 1 && !hc.usb_interrupt_registered());
    state.detach = true;
    assert(hc.usb_register_interrupt(0, 8, completed, &state));
    for (unsigned i = 0; i < 4; ++i) hc.usb_service_interrupts(timer);
    assert(state.calls == 2 && !hc.usb_interrupt_registered());
    assert(!hc.usb_transfer_pending());
    // The regression: a silent keyboard must not monopolize the controller.
    hc.hold_keyboard = true;
    assert(hc.usb_register_interrupt(0, 8, completed, &state));
    state.detach = false;
    hc.usb_service_interrupts(timer);
    unsigned calls_before = state.calls;
    for (unsigned i = 0; i < 100; ++i) {
        auto bulk = hc.usb_transfer(2, buffer, 8, timer, 100);
        assert(bulk.status_code == TRANSFER_SUCCESS && bulk.actual_length == 8);
        assert(state.calls == calls_before && hc.usb_transfer_pending());
    }
    // A report is dispatched/rearmed from inside a blocking storage wait.
    hc.keyboard_input = true;
    hc.completion_steps = 20;
    assert(hc.usb_transfer(2, buffer, 8, timer, 100).status_code == TRANSFER_SUCCESS);
    assert(state.calls == calls_before + 1 && hc.usb_transfer_pending());
    assert(state.saw_pending_bulk);
    hc.completion_steps = 3;
    hc.usb_unregister_interrupt(timer);
    while (hc.usb_interrupt_registered()) hc.usb_service_interrupts(timer);

    // Several endpoints, distinct buffers, out-of-order collection, retained IN
    // results and cancellation must not damage another endpoint's DMA/result.
    hc.hold_keyboard = false;
    u8 source[8] = {41,42,43,44,45,46,47,48};
    auto a = hc.usb_submit_transfer(0, nullptr, 8, timer);
    auto b = hc.usb_submit_transfer(1, source, 8, timer);
    auto c = hc.usb_submit_transfer(2, nullptr, 4, timer);
    assert(a.status_code == TRANSFER_PENDING && b.status_code == TRANSFER_PENDING &&
           c.status_code == TRANSFER_PENDING);
    memset(source, 0, sizeof(source));
    assert(hc.packet(b.request_id)[0] == 41);
    hc.usb_cancel_transfer(c.request_id, timer);
    for (unsigned i = 0; i < 3; ++i) hc.usb_service_transfers(timer);
    assert(hc.usb_read_transfer(a.request_id, buffer, 1, timer).status_code == INVALID_LENGTH);
    assert(hc.usb_submit_transfer(0, nullptr, 8, timer).status_code == TRANSFER_BUSY);
    assert(hc.usb_read_transfer(b.request_id, nullptr, 0, timer).status_code == TRANSFER_SUCCESS);
    assert(hc.usb_read_transfer(c.request_id, buffer, 8, timer).status_code == TRANSFER_CANCELLED);
    assert(hc.usb_read_transfer(a.request_id, buffer, 8, timer).status_code == TRANSFER_SUCCESS);
    for (unsigned i = 0; i < 8; ++i) assert(buffer[i] == i + 1);
    assert(hc.usb_read_transfer(b.request_id, nullptr, 0, timer).status_code == INVALID_REQUEST);

    // Timing is per request, including timer wrap; only polling one request
    // must still progress all the others and preserve their terminal results.
    timer.ticks = 0xfffffe;
    hc.hold_keyboard = true;
    a = hc.usb_submit_transfer(0, nullptr, 8, timer);
    b = hc.usb_submit_transfer(2, nullptr, 8, timer, 3);
    timer.ticks = 2;
    hc.usb_read_transfer(a.request_id, buffer, 8, timer);
    hc.usb_read_transfer(a.request_id, buffer, 8, timer);
    assert(hc.pending(a.request_id));
    assert(hc.usb_read_transfer(b.request_id, buffer, 8, timer).status_code == TIMEOUT);
    hc.usb_cancel_transfer(a.request_id, timer);
    while (hc.usb_read_transfer(a.request_id, buffer, 8, timer).status_code == TRANSFER_PENDING) {}

    // Capacity exhaustion is bounded and collecting one result releases only
    // that endpoint's slot. Stale IDs cannot collect its replacement.
    u64 ids[32] = {};
    assert(hc.capacity() == 32);
    for (unsigned i = 0; i < hc.capacity(); ++i) {
        auto r = hc.usb_submit_transfer(100 + i, nullptr, 0, timer);
        assert(r.status_code == TRANSFER_PENDING);
        ids[i] = r.request_id;
    }
    assert(hc.usb_submit_transfer(200, nullptr, 0, timer).status_code == TRANSFER_BUSY);
    for (unsigned i = 0; i < 3; ++i) hc.usb_service_transfers(timer);
    assert(hc.usb_read_transfer(ids[5], nullptr, 0, timer).status_code == TRANSFER_SUCCESS);
    auto replacement = hc.usb_submit_transfer(200, nullptr, 0, timer);
    assert(replacement.status_code == TRANSFER_PENDING && replacement.request_id != ids[5]);
    assert(hc.usb_read_transfer(ids[5], nullptr, 0, timer).status_code == INVALID_REQUEST);
    for (unsigned i = 0; i < hc.capacity(); ++i) if (i != 5)
        assert(hc.usb_read_transfer(ids[i], nullptr, 0, timer).status_code == TRANSFER_SUCCESS);
    for (unsigned i = 0; i < 3; ++i) hc.usb_service_transfers(timer);
    assert(hc.usb_read_transfer(replacement.request_id, nullptr, 0, timer).status_code == TRANSFER_SUCCESS);
    assert(!hc.usb_transfer_pending());
    assert(irq_begins > 100 && irq_begins == irq_ends);
    puts("USB asynchronous lifecycle, endpoint concurrency and interrupt subscription tests passed");
}
