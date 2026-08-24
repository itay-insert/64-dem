#ifndef APIC_H
#define APIC_H

#include "uint_definitions.h"
#include "c_compat.h"
#define IOAPIC_REGSEL 0x00
#define IOAPIC_WINDOW 0x10

KERNEL_EXTERN_C_BEGIN
u32 ioapic_read_register(u32 offset);
void ioapic_write_register(u32 offset, u32 value);
void APIC_init(void);
KERNEL_EXTERN_C_END

#endif
