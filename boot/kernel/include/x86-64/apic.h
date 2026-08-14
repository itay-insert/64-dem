#ifndef APIC_H
#define APIC_H

#include "uint_definitions.h"
#define IOAPIC_REGSEL 0x00
#define IOAPIC_WINDOW 0x10

u32 ioapic_read_register(u32 offset);
void ioapic_write_register(u32 offset, u32 value);
void APIC_init(void);

#endif