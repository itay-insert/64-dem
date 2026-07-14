#include "uint_definitions.h"
#include "memory.h"
#include "lowlevel.h"

#define IOAPIC_REGSEL 0x00
#define IOAPIC_WINDOW 0x10

extern u64 APIC_base;
extern u64 IO_APIC;

u32 ioapic_read_register(u32 offset) {
    ioapic_write(IOAPIC_REGSEL, offset);
    return ioapic_read(IOAPIC_WINDOW);
}

void ioapic_write_register(u32 offset, u32 value) {
    ioapic_write(IOAPIC_REGSEL, offset);
    ioapic_write(IOAPIC_WINDOW, value);
}

#define LAPIC_EOI 0xB0
#define LVT_TIMER 0x320
#define TIMER_INITIAL 0x380
#define TIMER_CURRENT 0x390
#define TIMER_DIVIDE 0x3E0

#define TIMER_VECTOR 0x40
#define TIMER_PERIODIC (1U << 17)


#define LVT_THERMAL 0x330
#define LVT_PERF 0x340
#define LVT_LINT0 0x350
#define LVT_LINT1 0x360
#define LVT_ERROR 0x370

void lapic_mask_all(void) {
    lapic_write(LVT_TIMER,   lapic_read(LVT_TIMER)   | (1 << 16));
    lapic_write(LVT_THERMAL, lapic_read(LVT_THERMAL) | (1 << 16));
    lapic_write(LVT_PERF,    lapic_read(LVT_PERF)    | (1 << 16));
    lapic_write(LVT_LINT0,   lapic_read(LVT_LINT0)   | (1 << 16));
    lapic_write(LVT_LINT1,   lapic_read(LVT_LINT1)   | (1 << 16));
    lapic_write(LVT_ERROR,   lapic_read(LVT_ERROR)   | (1 << 16));
}

void ioapic_mask_all(void) {
    u32 version = ioapic_read_register(1);
    u8 max = (version >> 16) & 0xff;

    for(u8 i = 0; i <= max; i++) {
        u8 reg = 0x10 + i * 2;

        u32 low = ioapic_read_register(reg);
        low |= (1 << 16);

        ioapic_write_register(reg, low);
    }
}

void APIC_init(void) {
    u64 apic = read_msr(0x1B);
    apic |= (1ULL << 11);
    write_msr(0x1B, apic);

    lapic_write(0xF0, 0x1FF);

    lapic_write(0x80, 0);

    lapic_mask_all();

    lapic_write(0x280, 0);

    ioapic_mask_all();
}

