#include "uint_definitions.h"
#include <stdbool.h>
#include "drivers/display/vga.h"
#include "x86-64/apic.h"
#include "x86-64/lowlevel.h"

extern void set_start(void);
extern void erase_from_cursor(void);
extern void enable_reverse_video(void);
extern void reset_video(void);
extern void clear_screen(void);

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

volatile u64 cursor_ticks = 0;

bool blink = false;

char stdout[4096] = {0};
int stdout_pc = 0;
int stdout_wc = 0;


static inline void stdout_exec(int arg0, int arg1, char ch) {
    switch (ch) {
        case 'H': {
            cursor_Setpos(arg1-1, arg0-1);
            return;
        }


        case 'C': {
            move_cursor(arg0);
            return;
        }

        case 'L': {
            
        }
    }
}


static inline int stdout_parse(int index) {
    index = (index + 1) % 4096;
    if (stdout[index] == 'H') { // ESC [ H: set cursor top-left
        set_start();
        index = (index + 1) % 4096;
        return index;
    } else if (stdout[index] == 'K') { // ESC [ K: erase from cursor to the end of the line
        erase_from_cursor();
        index = (index + 1) % 4096;
        return index;
    } else if (stdout[index] == '7' && stdout[(index + 1) % 4096] == 'm') { // ESC [ 7 m: enable reverse video
        enable_reverse_video();
        index = (index + 2) % 4096;
        return index;
    } else if (stdout[index] == '0' && stdout[(index + 1) % 4096] == 'm') { // ESC [ 0 m: restore video defaults
        reset_video();
        index = (index + 2) % 4096;
        return index;
    } else if (stdout[index] == '2' && stdout[(index + 1) % 4096] == 'J') { // ESC [ 2 J: clear screen
        clear_screen();
        index = (index + 2) % 4096;
        return index;
    }


    int arg_buffer[2] = {0};

    for (int i = 0; i < 2; i++) {
        int arg = 0;
        unsigned int digit = ascii_to_digit(stdout[index]);
        while (digit != 0xFFFFFFFF) {
            arg = (arg * 10) + digit;
            index = (index + 1) % 4096;
            digit = ascii_to_digit(stdout[index]);
        }

        if (stdout[index] != ';') {
            arg_buffer[i] = arg;
            break;
        }

        arg_buffer[i] = arg;
        index = (index + 1) % 4096;
    }


    stdout_exec(arg_buffer[0], arg_buffer[1], stdout[index]);
    index = (index + 1) % 4096;
    return index;
}



void timer_handler(u64 cs, u64 rip) {
	timer_ticks++;
    cursor_ticks++;

    if (cursor_ticks == 500) {
        cursor_ticks = 0;
        if (!blink) {
            draw_cursor(LightGray);
            blink = true;
        } else if (blink) {
            draw_cursor(Black);
            blink = false;
        }
    }

    usb_background_tick(cs, rip);

    while (stdout_pc != stdout_wc) {
        if (stdout[stdout_pc] == '\x1b') {
            stdout_pc = stdout_parse((stdout_pc+1) % 256);
        } else {
            printf("%c", stdout[stdout_pc]);
            stdout_pc = (stdout_pc + 1) % 256;
        }
    }

	lapic_write(0xB0, 0);
}


u64 get_ticks(void) {
	return timer_ticks;
}


void write(char *buf, int len) {
    for (int i = 0; i < len; i++) {
        stdout[(stdout_wc + i) % 4096] = buf[i];
    }

    stdout_wc = (stdout_wc + len) % 4096;

}
