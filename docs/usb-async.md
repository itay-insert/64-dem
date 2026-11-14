# USB asynchronous packet API

All four host controllers expose the same foreground/service-context API through
`usb_controller`. It submits one bulk or interrupt packet, bounded by the endpoint's
maximum packet size. It does not install hardware interrupt vectors.

## Reading a keyboard report

After enumeration, SET_CONFIGURATION and SET_PROTOCOL(0) have succeeded:

```cpp
// Submit once. No caller-owned input buffer is retained by the controller.
auto submitted = controller->usb_submit_transfer(endpoint_handle, nullptr, 8, timer);
if (submitted.status_code == TRANSFER_PENDING)
    keyboard_request_id = submitted.request_id;

// On subsequent main-loop iterations (not inside a waiting loop):
auto result = controller->usb_read_transfer(keyboard_request_id, report, 8, timer);
if (result.status_code == TRANSFER_SUCCESS) {
    // Validate result.actual_length, decode report, and submit another request.
} else if (result.status_code == TRANSFER_PENDING) {
    // Do other work; poll again on a later iteration.
} else {
    // Handle error; don't blindly rearm a controller needing recovery.
}
```

The endpoint handle retains the existing layout: port array index in bits 16–23,
interface array index in bits 8–15, endpoint array index in bits 0–7. The controller
object is the request's owner. Request ID zero is invalid; stale IDs cannot collect
a later request.

## Ownership and results

- OUT submissions copy their bytes before returning, so the caller may immediately
  reuse the source buffer. IN submissions may pass null as the source buffer.
- Reads copy completed IN bytes into a normal caller buffer. Pending reads do not
  write that buffer. No caller pointer survives a call.
- A terminal result is collected once. If a successful IN result needs a larger
  buffer, INVALID_LENGTH leaves it available for another read; actual_length tells
  the caller how many bytes are required.
- There is one outstanding/uncollected request per endpoint, with 32 request
  slots per controller. Different endpoints can run concurrently: an idle keyboard
  interrupt-IN read does not prevent bulk storage reads/writes on that controller.
  Submitting to an occupied endpoint, or exhausting the controller's request slots,
  returns TRANSFER_BUSY. A completed result still owns its endpoint and DMA buffer
  until collected. Requests are not queued behind another packet on the same endpoint.
- UHCI supports multiple scheduled endpoint QHs. OHCI/EHCI still admit only one
  outstanding periodic endpoint per controller because their periodic-table helpers
  do not implement a tree or bandwidth admission. Another periodic endpoint returns
  TRANSFER_BUSY; bulk endpoints remain independent. There is still only one recurring
  interrupt subscription per controller, including on xHCI.
- `usb_cancel_transfer(id, timer)` marks cancellation. Keep calling read until a
  terminal result is collected. A packet that completed before cancellation may
  still return success. Do not reset/free controller state while cancellation is
  pending.
- Async timeout zero means no packet deadline. A nonzero timeout is in milliseconds
  and includes setup. Hardware schedule transitions have independent bounded
  deadlines. Timer accounting follows the existing 24-bit sampling convention;
  service requests at least once per timer wrap (about 4.7 seconds for ACPI PM).
- Submission and read do not call wait_ms. Read advances schedule/cleanup state
  machines; it may take several polls after DMA completion before memory is safe
  to collect. These APIs are not advertised as IRQ-safe: initialization may allocate
  DMA, and legacy completion processing can emit diagnostics.
- The original blocking `usb_transfer` is a submit/read wrapper. Its timeout zero
  is treated as a finite one-millisecond deadline. Callers can use it unchanged.
  Its waits progress every active endpoint and collect/rearm the interrupt
  subscription, so keyboard callbacks can run during a blocking storage call.
  Each iteration also acknowledges/drains controller IRQ events and reenables
  delivery, rather than leaving the source masked until the bulk call returns.
  Callbacks run outside the request guard and should keep their work bounded.
- `usb_read_transfer` advances all active requests, preserving other callers'
  completed results. `usb_service_transfers(timer)` also progresses every request
  without collecting results. Foreground interrupt service includes this progress;
  callers using raw asynchronous I/O still collect their own request IDs.

## Controller behavior

UHCI waits for halt/start using polled states. OHCI waits for a new frame after
disabling a list, then reaps DoneHead references before recycling either TD.
EHCI waits for the correct ASS/PSS transition before editing its async/periodic
list. A per-controller schedule editor serializes these bounded legacy transitions,
not the lifetime of a pending packet. Schedule enable state is sampled at each edit,
so retiring one endpoint cannot restore a stale state and disable another endpoint.
UHCI briefly pauses the controller for an edit; OHCI/EHCI pause the affected list.
These transitions can add latency; they are not bandwidth or real-time guarantees.

OHCI routes each TD in the shared DoneHead to its owning request before acknowledging
WDH, including when a synchronous control transfer drains that queue. xHCI routes
Transfer Events by slot, endpoint ID and expected TRB to independent request records;
its synchronous EP0 waiter is separate from the bulk/interrupt routes.

Synchronous EP0 calls finish any in-progress schedule edit before installing their
control descriptors. They do not wait for idle keyboard reads to complete. UHCI
keeps existing endpoint QHs reachable while EP0 runs. Control calls retain polling
and dispatch no foreground callbacks until they return. Enumeration, endpoint
reconfiguration and controller destruction still require callers to quiesce I/O.

UHCI/OHCI can cancel an unexecuted packet after quiescence. EHCI/xHCI cancellation
and timeout conservatively require controller recovery; xHCI retains its DMA
allocation because Stop Endpoint/Set TR Dequeue recovery is not implemented.
Schedule/retirement failures quarantine controller-owned resources instead of
allowing unsafe descriptor reuse. No DMA points to the caller's buffer.

Existing hardware coverage limits remain: UHCI interval-aware scheduling is not
implemented; EHCI supports high-speed, ordinary interrupt endpoints with bInterval
1–14, without transaction-translator splits or high-bandwidth transfers. Bulk and
interrupt calls remain single-packet operations.

## Validation and references

`tests/test_usb_async.cpp` exercises pending/completion collection, retained results,
OUT-buffer ownership, stale IDs, cancellation, wrapping timers, and the blocking
wrapper. `tests/test_usb_periodic.cpp` checks interval/mask planning and schedule
installation/removal. These are host tests, not hardware emulation.

References: [Linux USB host API](https://docs.kernel.org/driver-api/usb/usb.html),
[OHCI specification](https://www.cs.usfca.edu/~cruse/cs698f11/hcir1_0a.pdf),
[EHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/ehci-specification-for-usb.pdf),
[xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf).


## Foreground interrupt subscriptions

`usb_register_interrupt(handle, length, callback, context)` installs one recurring
interrupt-IN subscription per controller. Registration validates the discovered
endpoint and accepts reports up to 64 bytes and no larger than its packet size.
`HIDK::init()` automatically starts `GlobalUSB::start_background_service(timer)`.
The kernel independently calibrates and starts a roughly 1 kHz LAPIC timer,
verifies that its interrupt counter advances, and services USB while interrupted code
is in user mode or at the kernel's explicit `interrupt_idle()` safe point. No
foreground service call is needed before entering a user program. This also
advances polling-only controllers and schedule transitions without completion IRQs.
The worker skips arbitrary kernel execution to avoid reentering USB, allocator,
and console operations. Long kernel operations still need foreground servicing;
blocking bulk waits already provide it. The boot-CPU USB and timer objects must
remain alive. The timer entry preserves x87/SSE state; AVX state is not enabled
or managed by this kernel path.
The first service submits a read; later calls collect it, invoke the callback,
and rearm on success. An occupied subscription endpoint or unavailable request slot is retried later.
Callbacks receive a terminal result and controller-owned data valid only during
the call (null on submission failure). Errors stop the subscription. Recursive
service calls are ignored; callbacks may unregister themselves.

`usb_unregister_interrupt(timer)` detaches the callback immediately and requests
cancellation. Keep servicing until `usb_interrupt_registered()` is false before
reusing/destroying controller state. The callback context can be released after
unregister returns, provided no foreground callback is executing. These APIs are
boot-CPU APIs, not thread-safe registration methods. Background callbacks must
not sleep; registration and teardown must run in kernel context.

`HIDK::init()` selects boot protocol, registers the keyboard callback, and starts
its first read. Boot reports become PC Set 1 make/break bytes in the inherited
`keyboard::buffer` and US-layout ASCII in `keyboard::ascii`. Both are 256-byte
circular write buffers using the existing counters. Consumers must track their
own progress; old bytes are overwritten. Typematic repetition, hotplug discovery,
and automatic recovery after a terminal controller error are not implemented.
The current kernel discovers devices at startup. The idle loop uses
`interrupt_idle()` so background servicing is safe while it waits for input;
blocking bulk waits also service the same controller's subscription.

## Controller IRQ setup

Every successfully initialized controller calls `usb_setup_irq()` after boot-time
port enumeration. It allocates a vector from 0x50–0x7f and configures single-message
PCI MSI, or MSI-X table entry zero when MSI is unavailable. The target is the boot
CPU's xAPIC ID. MSI-X table storage is mapped uncached, bounds-checked against its
BAR, and unused entries are masked. Failed setup leaves controller sources masked
and keeps the existing polling path available. Boot logs explicitly report MSI,
MSI-X, or polling for each controller.

The IDT contains an assembly stub for every reserved device vector. Stubs preserve
integer registers, clear DF, align the stack for C, dispatch the registered context,
acknowledge the Local APIC, and return with `iretq`. Both the dispatcher and USB top
half are compiled with `-mgeneral-regs-only`; they do not use SIMD/FPU state, allocate,
log, take foreground locks, invoke keyboard callbacks, or modify DMA schedules.

The USB top half checks the source and masks controller interrupt delivery, retaining
status and DMA ownership for foreground processing. This deasserts the source before
APIC EOI without prematurely clearing OHCI WDH or consuming xHCI events. IRQ counters
are available through `usb_irq_count()` and route status through `usb_irq_ready()`.
The counter records asserted sources handled by the top half; a queued vector
arriving after foreground polling cleared its source is ignored and not counted.

`GlobalUSB::service_interrupts(timer)` masks sources, acknowledges UHCI/EHCI transfer
status, routes OHCI DoneHead to each request's retirement watch, drains the
xHCI event ring, services subscriptions, and reenables interrupt delivery. Fatal
controller errors leave interrupts masked and mark the controller for recovery.
UHCI TDs and EHCI qTDs now request completion interrupts; OHCI and xHCI already did.
The background timer advances schedule/retirement state machines and timeout
accounting even when no hardware interrupt arrives, at the safe points above.
Blocking control/command waits retain polling so an IRQ arriving during boot work
cannot deadlock a caller waiting for foreground dispatch.

PCI INTx routing is not inferred from the Interrupt Line byte. The kernel has no
ACPI AML `_PRT`/interrupt-link resolver, so INTx-only controllers automatically use
polling. MSI/MSI-X setup currently targets the boot CPU's 8-bit xAPIC ID and is not
an SMP interrupt migration or interrupt-remapping implementation. MSI-X MMIO uses
the existing direct-map window and rejects physical addresses at or above 1 TiB.
No hotplug reenumeration or fatal-controller recovery is added by IRQ setup.

Keyboard, bulk and synchronous control traffic can share one controller. Hardware
interrupt setup does not change the request ownership or foreground-service rules
above.

Controller register references: [Intel xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf),
[Intel EHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/ehci-specification-for-usb.pdf),
[OHCI specification](https://www.cs.usfca.edu/~cruse/cs698f11/hcir1_0a.pdf), and
[Linux PCI MSI guide](https://www.kernel.org/doc/html/v6.6/PCI/msi-howto.html).

### IRQ validation

`tests/run_tests.sh` includes PCI config-space tests for 32/64-bit MSI, vector
masking, MSI-X table masking and BAR bounds, invalid BIRs, mapping failures, and
configuration-write rollback. USB top-half tests verify source masking, repeated
IRQ suppression, and retention of OHCI WDH for foreground processing.

Headless QEMU checks used `qemu-xhci` with `usb-kbd` in three modes:
`msi=on,msix=off`, `msi=off,msix=on`, and `msi=off,msix=off`. Monitor-injected
`sendkey a` and `sendkey shift-b` produced `aB` in each case. QEMU `-d int` traces
confirmed repeated deliveries of vector 0x50 in MSI/MSI-X modes and zero deliveries
of that vector in polling mode. Legacy controller hardware IRQ paths have not
been verified on physical machines.


Keyboard selection retains the first boot-keyboard interrupt-IN endpoint in
configuration descriptor order and publishes it only after device configuration
succeeds. A later interface or another device cannot silently overwrite that
selection. `selected keyboard port ... interface ... endpoint ...` identifies the
actual choice; the earlier `HID keyboard found` lines only identify candidates.

Idle interrupt-IN requests remain pending silently until input arrives. Only
initialization, first-report, and actual transfer-error diagnostics are printed.

## Concurrent keyboard/storage validation

Run `bash tests/run_tests.sh` for the host suite. The asynchronous tests cover an
idle keyboard during repeated bulk calls, callback delivery before a blocking bulk
request finishes, independent DMA/results, per-request cancellation and wrapping
deadlines, retained completions, capacity exhaustion and stale IDs.

`python3 tests/run_usb_concurrency_qemu.py --work-dir /tmp/usb-concurrency-check`
builds an isolated temporary kernel and boots QEMU with a keyboard and read-only
patterned mass-storage disk on the same controller. The directory must be empty.
It requires the normal UEFI/kernel build dependencies, QEMU, OVMF, dosfstools,
mtools and sfdisk. Use `--controller xhci --irq-mode msi` for one case.

The probe checks 144 READ(10) sectors per case, including 16 reads while the
keyboard is idle and subsequent reads while QMP injects keys. It also exercises
EP0 with keyboard and bulk IN requests outstanding. UHCI/OHCI/EHCI polling and
xHCI MSI/MSI-X/polling passed these data/input/control checks. The runner records
QEMU interrupt traces to distinguish actual vector delivery from source counters
that can miss vectors already acknowledged by foreground polling. Repeated xHCI
MSI vector 0x50 delivery was additionally verified after adding IRQ service to
blocking bulk waits. Physical hardware concurrency remains unverified.

The probe is test-only; it does not change the production kernel or implement the
mass-storage abstraction. Existing single-packet limits and conservative
EHCI/xHCI timeout/cancellation recovery requirements still apply.
