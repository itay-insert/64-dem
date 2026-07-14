#include "uint_definitions.h"
#include "memory.h"

#ifndef LOWLEVEL_H
#define LOWLEVEL_H

u64 stack_get(void);
int check_1gb_PageSupport(void);
void enable_paging(u64 PML4_address);
void kernel_trampoline(u64 *info_buffer64, int *info_buffer, u64 stack, EFI_MEMORY_DESCRIPTOR *memory_map, u64 entry);
void load_gdt(u64 addr);
u64 discover_APIC(void);
void load_idt(u64 addr);
void disable_pic(void);
u64 read_msr(u32 offset);
void write_msr(u32 offset, u64 value);
u32 lapic_read(u32 offset);
void lapic_write(u32 offset, u32 value);
u32 ioapic_read(u32 offset);
void ioapic_write(u32 offset, u32 value);
void enable_interrupts(void);
void disable_interrupts(void);

#endif
