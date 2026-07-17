#include "uint_definitions.h"
#ifndef ACPI_H
#define ACPI_H


typedef struct {
    u64 io_base;
    int code;
} PM_ret;

typedef struct {
    char signature[4];
    u64 Address;
    PM_ret simple_timer;
    int status;
} ACPI_ret;

u64 find_rsdp_legacy(void);
void rsdp_init(u64 rsdp_base);
ACPI_ret ACPI_discovery(const char *signature);

#endif