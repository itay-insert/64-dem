#include "uint_definitions.h"
#ifndef ACPI_H
#define ACPI_H

u64 find_rsdp_legacy(void);
void rsdp_init(u64 rsdp_base);
u64 discover_APICIO(void);

#endif