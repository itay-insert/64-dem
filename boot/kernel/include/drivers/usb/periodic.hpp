#pragma once
#include <stdint.h>

// Planning for legacy controllers with one admitted periodic endpoint.
namespace usb_periodic {
class request_guard {
    unsigned &flag;
    bool owned;
public:
    explicit request_guard(unsigned &busy) : flag(busy),
        owned(__atomic_exchange_n(&busy, 1u, __ATOMIC_ACQUIRE) == 0) {}
    ~request_guard() {
        if (owned) __atomic_store_n(&flag, 0u, __ATOMIC_RELEASE);
    }
    explicit operator bool() const { return owned; }
    request_guard(const request_guard &) = delete;
    request_guard &operator=(const request_guard &) = delete;
};

inline unsigned ohci_period(unsigned interval) {
    if (!interval || interval > 255) return 0;
    unsigned period = 1;
    while (period < 32 && period * 2 <= interval) period *= 2;
    return period;
}

struct ehci_plan { unsigned frames; uint8_t smask; };
inline ehci_plan high_speed_plan(unsigned interval) {
    // A 1024-frame list represents up to 8192 microframes. Longer periods
    // need software gating rather than silently polling too frequently.
    if (!interval || interval > 14) return {0, 0};
    unsigned microframes = 1u << (interval - 1);
    if (microframes >= 8) return {microframes / 8, 1};
    uint8_t mask = 0;
    for (unsigned i = 0; i < 8; i += microframes) mask |= 1u << i;
    return {1, mask};
}

// Refuse occupied schedules. This avoids corrupting another endpoint's links
// until a periodic tree and bandwidth admission are implemented.
inline bool install(volatile uint32_t *table, unsigned entries,
                    unsigned period, uint32_t empty, uint32_t link) {
    if (!period || period > entries) return false;
    for (unsigned i = 0; i < entries; ++i)
        if (table[i] != empty) return false;
    for (unsigned i = 0; i < entries; i += period) table[i] = link;
    return true;
}
inline bool remove(volatile uint32_t *table, unsigned entries,
                   uint32_t empty, uint32_t link) {
    bool found = false;
    for (unsigned i = 0; i < entries; ++i) {
        if (table[i] != empty && table[i] != link) return false;
        if (table[i] == link) found = true;
    }
    if (!found) return false;
    for (unsigned i = 0; i < entries; ++i)
        if (table[i] == link) table[i] = empty;
    return true;
}
}
