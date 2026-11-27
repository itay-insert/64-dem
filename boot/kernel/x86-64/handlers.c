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
extern void make_blank_rows(int n);
extern void delete_rows(int n);

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
extern void terminate_procces(int index);
extern void SwitchProccesses(void);
extern int active_proccesses;
extern int current_proccess;


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
    if (active_proccesses > 0) {
        __asm__ volatile ("sti");
        terminate_procces(current_proccess);
    }
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


static void stdout_exec(int row, int col, char command) {
    int count = row ? row : 1;
    switch (command) {
        case 'H': case 'f': cursor_Setpos(col ? col - 1 : 0, count - 1); break;
        case 'A': console_move_cursor(0, -count); break;
        case 'B': console_move_cursor(0, count); break;
        case 'C': console_move_cursor(count, 0); break;
        case 'D': console_move_cursor(-count, 0); break;
        case 'K': if (!row) erase_from_cursor(); break;
        case 'J': if (row == 2) clear_screen(); break;
        case 'L': make_blank_rows(count); break;
        case 'M': delete_rows(count); break;
        case 'm':
            if (!row) reset_video();
            else if (row == 7) enable_reverse_video();
            break;
    }
}

// Leave an incomplete escape sequence queued until the next write.
static bool stdout_parse(void) {
    int index = (stdout_pc + 1) % sizeof(stdout);
    if (index == stdout_wc)
        return false;
    if (stdout[index] != '[') {
        stdout_pc = index;
        return true;
    }

    int args[2] = {0};
    int argument = 0;
    bool supported = true;
    for (index = (index + 1) % sizeof(stdout); index != stdout_wc;
         index = (index + 1) % sizeof(stdout)) {
        unsigned char ch = stdout[index];
        if (ch >= 0x40 && ch <= 0x7e) {
            if (supported)
                stdout_exec(args[0], args[1], ch);
            stdout_pc = (index + 1) % sizeof(stdout);
            return true;
        }
        if (ch < 0x20 || ch > 0x3f) {
            stdout_pc = index;
            return true;
        }
        if (ch >= '0' && ch <= '9' && argument < 2) {
            if (args[argument] < 10000)
                args[argument] = args[argument] * 10 + ch - '0';
        } else if (ch == ';' && argument == 0) {
            ++argument;
        } else {
            supported = false;
        }
    }
    return false;
}

u8 proccess_target = 20;
u8 proccess_ticks = 0;


void timer_handler(u64 cs, u64 rip) {
	timer_ticks++;
    cursor_ticks++;
    proccess_ticks++;

    if (cursor_ticks == 500) {
        cursor_ticks = 0;
        if (!blink) {
            draw_cursor(LightGray);
            blink = true;
        } else if (blink) {
            hide_cursor();
            blink = false;
        }
    }


    usb_background_tick(cs, rip);

    if (stdout_pc != stdout_wc) {
        hide_cursor();
        while (stdout_pc != stdout_wc) {
            if (stdout[stdout_pc] == '\x1b') {
                if (!stdout_parse())
                    break;
            } else {
                console_put_char((unsigned char)stdout[stdout_pc]);
                stdout_pc = (stdout_pc + 1) % sizeof(stdout);
            }
        }
        console_present();
        if (blink)
            draw_cursor(LightGray);
    }


	lapic_write(0xB0, 0);

}


u64 get_ticks(void) {
	return timer_ticks;
}


extern int current_proccess;

void write(char *buf, int len) {
    if (render_rights != current_proccess)
        return;

    for (int i = 0; i < len; i++) {
        stdout[(stdout_wc + i) % 4096] = buf[i];
    }

    stdout_wc = (stdout_wc + len) % 4096;

}
