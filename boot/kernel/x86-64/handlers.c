#include "uint_definitions.h"
#include "vga.h"

static char exception_names[32][24] = {
    "Divide error", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND range", "Invalid opcode", "Device unavailable",
    "Double fault", "Reserved", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection", "Page fault", "Reserved",
    "x87 floating point", "Alignment check", "Machine check", "SIMD floating point",
    "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security", "Reserved"
};

static char unknown_exception[] = "Unknown";

__attribute__((noreturn))
void exception_handler(u64 vector, u64 error_code, u64 rip, u64 cs,
                       u64 rflags, u64 cr2) {
    char *name = vector < 32 ? exception_names[vector] : unknown_exception;

    printf("\nEXECPTION %lu: ", vector);
    printf(name);
    printf("\n");
    printf("error=0x%lx rip=0x%lx cs=0x%lx rflags=0x%lx\n",
           error_code, rip, cs, rflags);
    if (vector == 14)
        printf("cr2=0x%lx\n", cr2);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
