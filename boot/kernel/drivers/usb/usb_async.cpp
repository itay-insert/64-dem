#include "drivers/usb/usb.hpp"
#include "drivers/usb/periodic.hpp"
#include "x86-64/memory/memory_helpers.h"

usb_transfer_result usb_controller::transfer_result(int status, u32 handle) const {
    usb_transfer_result result = {};
    result.status_code = status;
    result.endpoint_handle = handle;
    result.controller_type = controller_type;
    result.controller_index = controller.index;
    return result;
}

bool usb_controller::phase_expired(TimerSource &timer, u32 ms) const {
    const u64 ticks = (timer.read_freq() * ms + 999) / 1000;
    return async.phase_elapsed >= ticks;
}

bool usb_controller::transfer_expired(TimerSource &timer) const {
    return async.timeout && async.elapsed >=
        (timer.read_freq() * async.timeout + 999) / 1000;
}

void usb_controller::transfer_phase(u32 phase) {
    async.phase = phase;
    async.phase_elapsed = 0;
}

void usb_controller::transfer_complete(int status) {
    async.result.status_code = status;
    async.completed = true;
}

usb_transfer_result usb_controller::usb_submit_transfer(
    u32 handle, const void *buffer, u32 length, TimerSource &timer, u32 timeout)
{
    usb_periodic::request_guard guard(async_api_busy);
    if (!guard || async.active) return transfer_result(TRANSFER_BUSY, handle);
    if (controller.needs_reinit || !timer.read_freq() || next_request_id == ~0ULL)
        return transfer_result(FAILED, handle);
    async = {};
    async.result = transfer_result(TRANSFER_PENDING, handle);
    async.length = length;
    async.timeout = timeout;
    async.previous_tick = (u32)timer.read() & 0xffffffu;
    async.active = true;
    usb_transfer_result result = start_transfer(handle, const_cast<void *>(buffer), length, timer);
    if (result.status_code != TRANSFER_PENDING) {
        async.active = false;
        return result;
    }
    // Never reuse IDs, including after collection. Zero is always invalid.
    async.result.request_id = ++next_request_id;
    return async.result;
}

usb_transfer_result usb_controller::usb_read_transfer(
    u64 id, void *buffer, u32 capacity, TimerSource &timer)
{
    usb_periodic::request_guard guard(async_api_busy);
    if (!guard) return transfer_result(TRANSFER_BUSY);
    if (!id || !async.active || id != async.result.request_id)
        return transfer_result(INVALID_REQUEST);
    if (!async.completed) {
        // Match wait_ms(): accumulate wrapping 24-bit timer samples. Service
        // pending requests at least once per timer wrap (about 4.7 s for PM).
        const u32 now = (u32)timer.read() & 0xffffffu;
        const u32 delta = (now - async.previous_tick) & 0xffffffu;
        async.previous_tick = now;
        async.elapsed += delta;
        async.phase_elapsed += delta;
        advance_transfer(timer);
    }
    if (!async.completed) return async.result;
    if (async.result.status_code == TRANSFER_SUCCESS && async.in &&
        async.result.actual_length) {
        if (!buffer || capacity < async.result.actual_length) {
            auto result = async.result;
            result.status_code = INVALID_LENGTH;
            return result; // Keep completion available for a larger buffer.
        }
        memcpy(buffer, async.dma_buffer, async.result.actual_length);
    }
    const auto result = async.result;
    async.active = false;
    return result;
}

usb_transfer_result usb_controller::usb_cancel_transfer(u64 id, TimerSource &timer) {
    (void)timer;
    usb_periodic::request_guard guard(async_api_busy);
    if (!guard) return transfer_result(TRANSFER_BUSY);
    if (!id || !async.active || id != async.result.request_id)
        return transfer_result(INVALID_REQUEST);
    async.cancel = true;
    return async.result; // Keep reading until cancellation/completion is collected.
}

usb_transfer_result usb_controller::usb_transfer(
    u32 handle, void *buffer, u32 length, TimerSource &timer, u32 timeout)
{
    if (length && !buffer) return transfer_result(INVALID_LENGTH, handle);
    auto result = usb_submit_transfer(handle, buffer, length, timer, timeout);
    if (result.status_code != TRANSFER_PENDING) return result;
    const u64 id = result.request_id;
    // Keep zero as a finite deadline for the legacy blocking API.
    if (!timeout) async.timeout = 1;
    do {
        result = usb_read_transfer(id, buffer, length, timer);
        if (result.status_code == TRANSFER_PENDING || result.status_code == TRANSFER_BUSY)
            wait_ms(timer, 1);
    } while (result.status_code == TRANSFER_PENDING || result.status_code == TRANSFER_BUSY);
    return result;
}


bool usb_controller::usb_register_interrupt(u32 handle, u32 length,
    usb_interrupt_callback callback, void *context)
{
    if (!callback || !length || length > sizeof(interrupt_buffer) ||
        interrupt_callback || interrupt_request || interrupt_servicing)
        return false;
    const u32 port = (handle >> 16) & 0xff;
    const u32 iface = (handle >> 8) & 0xff;
    const u32 ep = handle & 0xff;
    if ((handle >> 24) || !ports || port >= (u32)ports_count ||
        !ports[port].connected || !ports[port].device)
        return false;
    const usb_device &device = *ports[port].device;
    if (!device.discovered || !device.interfaces || iface >= device.usb_interface_count)
        return false;
    const usb_interface &interface = device.interfaces[iface];
    if (!interface.endpoints || ep >= interface.endpoint_count)
        return false;
    const usb_endpoint &endpoint = interface.endpoints[ep];
    if (endpoint.transfer_type != 3 || !(endpoint.address & IN) ||
        length > (endpoint.max_packet_size & 0x7ff))
        return false;
    interrupt_handle = handle;
    interrupt_length = length;
    interrupt_context = context;
    interrupt_stopping = false;
    interrupt_callback = callback;
    return true;
}

void usb_controller::usb_unregister_interrupt(TimerSource &timer) {
    interrupt_callback = nullptr;
    interrupt_context = nullptr;
    interrupt_stopping = true;
    if (interrupt_request) usb_cancel_transfer(interrupt_request, timer);
}

void usb_controller::usb_service_interrupts(TimerSource &timer) {
    if (interrupt_servicing) return;
    interrupt_servicing = true;
    if (interrupt_request) {
        auto result = usb_read_transfer(interrupt_request, interrupt_buffer,
                                        sizeof(interrupt_buffer), timer);
        if (result.status_code == TRANSFER_PENDING || result.status_code == TRANSFER_BUSY) {
            interrupt_servicing = false;
            return;
        }
        interrupt_request = 0;
        // Errors stop the subscription. A callback may explicitly recover later.
        auto callback = interrupt_callback;
        void *context = interrupt_context;
        if (result.status_code != TRANSFER_SUCCESS) {
            interrupt_callback = nullptr;
            interrupt_context = nullptr;
            interrupt_stopping = true;
        }
        if (callback) callback(context, result, interrupt_buffer);
    }
    if (interrupt_callback && !interrupt_stopping) {
        auto result = usb_submit_transfer(interrupt_handle, nullptr,
                                          interrupt_length, timer, 0);
        if (result.status_code == TRANSFER_PENDING) {
            interrupt_request = result.request_id;
        } else if (result.status_code != TRANSFER_BUSY) {
            auto callback = interrupt_callback;
            void *context = interrupt_context;
            interrupt_callback = nullptr;
            interrupt_context = nullptr;
            interrupt_stopping = true;
            callback(context, result, nullptr);
        }
    }
    interrupt_servicing = false;
}
