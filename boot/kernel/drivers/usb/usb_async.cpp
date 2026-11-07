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
