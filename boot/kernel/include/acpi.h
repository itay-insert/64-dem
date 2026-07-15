#include "uint_definitions.h"
#ifndef ACPI_H
#define ACPI_H


typedef struct {
    u64 io_base;
    int code;
} PM_ret;

u64 find_rsdp_legacy(void);
void rsdp_init(u64 rsdp_base);
u64 discover_IOAPIC(void);
PM_ret discover_PM_timer(void);

#endif