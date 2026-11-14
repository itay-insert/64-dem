#include "uint_definitions.h"
#include "drivers/display/vga.h"
#include "x86-64/apic.h"
#include "x86-64/lowlevel.h"

u64 former_ss = 0;
u64 former_cs = 0;
u64 former_rflags = 0;
u64 former_rsp = 0;
u64 former_rip = 0;

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

    printf("former_ss=0x%lx  former_cs=0x%lx  former_rip=0x%lx  former_rsp=0x%lx  former_rflags=0x%lx\n", former_ss, former_cs, former_rip, former_rsp, former_rflags);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static volatile u64 timer_ticks = 0;

extern void usb_background_tick(u64 cs, u64 rip);

void timer_handler(u64 cs, u64 rip) {
	timer_ticks++;

    usb_background_tick(cs, rip);

	lapic_write(0xB0, 0);
}


u64 get_ticks(void) {
	return timer_ticks;
}
