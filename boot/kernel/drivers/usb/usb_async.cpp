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

bool usb_controller::phase_expired(const usb_async_request &async, TimerSource &timer, u32 ms) const {
    return async.phase_elapsed >= (timer.read_freq() * ms + 999) / 1000;
}
bool usb_controller::transfer_expired(const usb_async_request &async, TimerSource &timer) const {
    return async.timeout && async.elapsed >= (timer.read_freq() * async.timeout + 999) / 1000;
}
void usb_controller::transfer_phase(usb_async_request &async, u32 phase) {
    async.phase = phase;
    async.phase_elapsed = 0;
}
void usb_controller::transfer_complete(usb_async_request &async, int status) {
    async.result.status_code = status;
    async.completed = true;
    release_schedule(async);
}
bool usb_controller::acquire_schedule(usb_async_request &request) {
    if (schedule_editor && schedule_editor != &request) return false;
    if (!schedule_editor) request.phase_elapsed = 0;
    schedule_editor = &request;
    return true;
}
void usb_controller::release_schedule(usb_async_request &request) {
    if (schedule_editor == &request) schedule_editor = nullptr;
}
usb_async_request *usb_controller::find_request(u64 id) {
    if (id) for (auto &request : requests)
        if (request.active && request.result.request_id == id) return &request;
    return nullptr;
}
void usb_controller::advance_requests(TimerSource &timer) {
    // Every endpoint gets service, including a schedule editor whose caller
    // is currently waiting on a different endpoint. Results stay owned until read.
    for (auto &request : requests) {
        if (!request.active || request.completed) continue;
        const u32 now = (u32)timer.read() & 0xffffffu;
        const u32 delta = (now - request.previous_tick) & 0xffffffu;
        request.previous_tick = now;
        request.elapsed += delta;
        request.phase_elapsed += delta;
        advance_transfer(request, timer);
    }
}
void usb_controller::usb_service_transfers(TimerSource &timer) {
    usb_periodic::request_guard guard(async_api_busy);
    if (guard) advance_requests(timer);
}

// Synchronous EP0 code may edit the same lists. Finish a bounded schedule
// transition first, without waiting for pending endpoint packets to complete.
// These APIs share one foreground context; no callbacks run inside this helper.
bool usb_controller::prepare_control(TimerSource &timer) {
    for (unsigned elapsed = 0; elapsed < 500; ++elapsed) {
        if (controller.needs_reinit) return false;
        if (!schedule_editor) return true;
        usb_service_transfers(timer);
        wait_ms(timer, 1);
    }
    return false;
}

usb_transfer_result usb_controller::usb_submit_transfer(
    u32 handle, const void *buffer, u32 length, TimerSource &timer, u32 timeout)
{
    usb_periodic::request_guard guard(async_api_busy);
    if (!guard) return transfer_result(TRANSFER_BUSY, handle);
    if (controller.needs_reinit || !timer.read_freq() || next_request_id == ~0ULL)
        return transfer_result(FAILED, handle);
    usb_async_request *available = nullptr;
    for (auto &request : requests) {
        if (request.active && request.result.endpoint_handle == handle)
            return transfer_result(TRANSFER_BUSY, handle);
        if (!request.active && !available) available = &request;
    }
    if (!available) return transfer_result(TRANSFER_BUSY, handle);
    auto &async = *available;
    async = {};
    async.result = transfer_result(TRANSFER_PENDING, handle);
    async.length = length;
    async.timeout = timeout;
    async.previous_tick = (u32)timer.read() & 0xffffffu;
    async.active = true;
    auto result = start_transfer(async, handle, const_cast<void *>(buffer), length, timer);
    if (result.status_code != TRANSFER_PENDING) {
        release_schedule(async);
        async.active = false;
        return result;
    }
    async.result.request_id = ++next_request_id;
    return async.result;
}

usb_transfer_result usb_controller::usb_read_transfer(
    u64 id, void *buffer, u32 capacity, TimerSource &timer)
{
    usb_periodic::request_guard guard(async_api_busy);
    if (!guard) return transfer_result(TRANSFER_BUSY);
    auto *request = find_request(id);
    if (!request) return transfer_result(INVALID_REQUEST);
    advance_requests(timer);
    auto &async = *request;
    if (!async.completed) return async.result;
    if (async.result.status_code == TRANSFER_SUCCESS && async.in && async.result.actual_length) {
        if (!buffer || capacity < async.result.actual_length) {
            auto result = async.result;
            result.status_code = INVALID_LENGTH;
            return result;
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
    auto *request = find_request(id);
    if (!request) return transfer_result(INVALID_REQUEST);
    request->cancel = true;
    return request->result;
}
usb_transfer_result usb_controller::usb_transfer(
    u32 handle, void *buffer, u32 length, TimerSource &timer, u32 timeout)
{
    if (length && !buffer) return transfer_result(INVALID_LENGTH, handle);
    // Zero retains the blocking API's finite one-millisecond deadline.
    auto result = usb_submit_transfer(handle, buffer, length, timer, timeout ? timeout : 1);
    if (result.status_code != TRANSFER_PENDING) return result;
    const u64 id = result.request_id;
    do {
        usb_begin_irq_service();
        result = usb_read_transfer(id, buffer, length, timer);
        // Callbacks run outside the request guard. A long bulk wait must not
        // postpone keyboard collection and rearming until the disk finishes.
        if (usb_interrupt_registered()) usb_service_interrupts(timer);
        usb_end_irq_service();
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
    if (!interrupt_request) usb_service_transfers(timer);
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
