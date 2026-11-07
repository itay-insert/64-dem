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
- There is one outstanding/uncollected request per controller. A second submission
  returns TRANSFER_BUSY. A pending keyboard request therefore currently prevents
  storage transfers and EP0 requests on that same controller. Multiple concurrent
  endpoint queues remain future work.
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

## Controller behavior

UHCI waits for halt/start using polled states. OHCI waits for a new frame after
disabling a list, then reaps DoneHead references before recycling either TD.
EHCI waits for the correct ASS/PSS transition before editing its async/periodic
list. xHCI uses its configured endpoint ring and event waiter.

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
