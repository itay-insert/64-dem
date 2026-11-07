#include <assert.h>
#include <stdio.h>
#include "drivers/usb/periodic.hpp"

int main() {
    using namespace usb_periodic;
    unsigned busy = 0;
    {
        request_guard first(busy);
        assert(first);
        { request_guard second(busy); assert(!second); }
        assert(busy == 1);
    }
    assert(busy == 0);
    assert(ohci_period(0) == 0);
    for (unsigned interval = 1; interval <= 255; ++interval) {
        unsigned p = ohci_period(interval);
        assert(p && !(p & (p - 1)) && p <= 32 && p <= interval);
        assert(p == 32 || p * 2 > interval);
    }
    assert(ohci_period(256) == 0);
    assert(high_speed_plan(0).frames == 0);
    assert(high_speed_plan(15).frames == 0);
    assert(high_speed_plan(16).frames == 0);
    for (unsigned interval = 1; interval <= 14; ++interval) {
        auto plan = high_speed_plan(interval);
        unsigned period = 1u << (interval - 1);
        unsigned previous = 0, count = 0;
        for (unsigned microframe = 0; microframe < 8192; ++microframe) {
            bool scheduled = ((microframe / 8) % plan.frames == 0) &&
                             (plan.smask & (1u << (microframe % 8)));
            if (!scheduled) continue;
            if (count) assert(microframe - previous == period);
            previous = microframe;
            ++count;
        }
        assert(count == 8192 / period);
    }
    uint32_t table[1024];
    for (unsigned i = 0; i < 1024; ++i) table[i] = 1;
    assert(install(table, 1024, 8, 1, 0x1022));
    for (unsigned i = 0; i < 1024; ++i)
        assert(table[i] == (i % 8 ? 1u : 0x1022u));
    assert(!install(table, 1024, 1, 1, 0x2022));
    assert(!remove(table, 1024, 1, 0x2022));
    assert(table[0] == 0x1022); // Failed edits don't clobber another endpoint.
    assert(remove(table, 1024, 1, 0x1022));
    for (unsigned i = 0; i < 1024; ++i) assert(table[i] == 1);
    assert(!remove(table, 1024, 1, 0x1022));
    uint32_t hcca[32] = {};
    assert(install(hcca, 32, 16, 0, 0x1000));
    assert(hcca[0] == 0x1000 && hcca[16] == 0x1000 && hcca[1] == 0);
    assert(remove(hcca, 32, 0, 0x1000));
    for (unsigned i = 0; i < 32; ++i) assert(hcca[i] == 0);
    puts("USB periodic scheduling tests passed");
}
