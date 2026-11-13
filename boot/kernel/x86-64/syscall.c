#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include "uint_definitions.h"
#include "x86-64/lowlevel.h"
#include "x86-64/interrupts.h"
#include "drivers/display/vga.h"

#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084

#define KNL_CS 0x08
#define KNL_DATA  0x10
#define USR_CS 0x18
#define USR_DATA 0x20
#define TSS 0x28


void syscall_init(void) {
    u64 efer = read_msr(IA32_EFER);
    write_msr(IA32_EFER, efer | 1);

    u64 star = ((u64)(USR_CS - 16) << 48) | ((u64)KNL_CS << 32);
    write_msr(IA32_STAR, star);

    u64 syscall_entry = get_syscall();
    write_msr(IA32_LSTAR, syscall_entry);

    write_msr(IA32_FMASK, (1 << 9) | (1 << 10));
}


u64 syscall_handler(u64 syscall_number, ...) {
    va_list args;

    va_start(args, syscall_number);

    const char *format = va_arg(args, const char *);
    printf(format, args);

    va_end(args);
    return 0;
}

