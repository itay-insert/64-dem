#include "uint_definitions.h"
#include "memory.h"
#include "boot_info.h"

#ifndef LOWLEVEL_H
#define LOWLEVEL_H

u64 stack_get(void);
int check_1gb_PageSupport(void);
void enable_paging(u64 PML4_address);
void kernel_trampoline(BOOT_INFO64 *info64, BOOT_INFO32 *info32, u64 stack,
                       EFI_MEMORY_DESCRIPTOR *memory_map, u64 entry);
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
u64 MMIO_read64(u64 base, u64 offset);
void MMIO_write64(u64 base, u64 offset, u64 value);
u32 MMIO_read32(u64 base, u64 offset);
void MMIO_write32(u64 base, u64 offset, u32 value);
u16 MMIO_read16(u64 base, u64 offset);
void MMIO_write16(u64 base, u64 offset, u16 value);
u8 MMIO_read8(u64 base, u64 offset);
void MMIO_write8(u64 base, u64 offset, u8 value);

#endif
