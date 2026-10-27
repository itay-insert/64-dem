#include "x86-64/ports.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "x86-64/memory/memory.h"
#include "drivers/acpiPM.hpp"

extern GlobalKeyboard *globalkeyboard;

extern u64 acpiadd;

void PS2K::init() {
    memcpy(vendor, "PS2", 4);
    scancode_top = 0;
}

bool set = false;
volatile u64 keyboard_irq_count = 0;


struct KeyMap {
    u8 scancode;
    char normal;
    char shifted;
};

static const KeyMap keymap[] = {
    {0x02, '1', '!'}, {0x03, '2', '@'},
    {0x04, '3', '#'}, {0x05, '4', '$'},
    {0x06, '5', '%'}, {0x07, '6', '^'},
    {0x08, '7', '&'}, {0x09, '8', '*'},
    {0x0A, '9', '('}, {0x0B, '0', ')'},
    {0x0C, '-', '_'}, {0x0D, '=', '+'},
    {0x0E, '\b', '\b'}, {0x0F, '\t', '\t'},

    {0x10, 'q', 'Q'}, {0x11, 'w', 'W'},
    {0x12, 'e', 'E'}, {0x13, 'r', 'R'},
    {0x14, 't', 'T'}, {0x15, 'y', 'Y'},
    {0x16, 'u', 'U'}, {0x17, 'i', 'I'},
    {0x18, 'o', 'O'}, {0x19, 'p', 'P'},
    {0x1A, '[', '{'}, {0x1B, ']', '}'},
    {0x1C, '\n', '\n'},

    {0x1E, 'a', 'A'}, {0x1F, 's', 'S'},
    {0x20, 'd', 'D'}, {0x21, 'f', 'F'},
    {0x22, 'g', 'G'}, {0x23, 'h', 'H'},
    {0x24, 'j', 'J'}, {0x25, 'k', 'K'},
    {0x26, 'l', 'L'}, {0x27, ';', ':'},
    {0x28, '\'', '"'}, {0x29, '`', '~'},
    {0x2B, '\\', '|'},

    {0x2C, 'z', 'Z'}, {0x2D, 'x', 'X'},
    {0x2E, 'c', 'C'}, {0x2F, 'v', 'V'},
    {0x30, 'b', 'B'}, {0x31, 'n', 'N'},
    {0x32, 'm', 'M'}, {0x33, ',', '<'},
    {0x34, '.', '>'}, {0x35, '/', '?'},

    {0x37, '*', '*'}, {0x39, ' ', ' '},
    {0x4A, '-', '-'}, {0x4E, '+', '+'}
};

static char lookup_scancode(u8 code, u8 shift, u8 caps_lock)
{
    for (u32 i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++) {
        if (keymap[i].scancode != code)
            continue;

        char c = keymap[i].normal;

        if (c >= 'a' && c <= 'z')
            return (caps_lock ^ shift)
                ? keymap[i].shifted
                : c;

        return shift ? keymap[i].shifted : c;
    }

    return 0;
}

static bool ps2_wait_input_empty()
{
    for (u32 i = 0; i < 100000; ++i) {
        if (!(inb(0x64) & 0x02))
            return true;
    }

    return false;
}

static bool ps2_wait_output_full()
{
    for (u32 i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01)
            return true;
    }

    return false;
}

static bool ps2_command(u8 command)
{
    if (!ps2_wait_input_empty())
        return false;

    outb(0x64, command);
    return true;
}

static bool ps2_write_data(u8 data)
{
    if (!ps2_wait_input_empty())
        return false;

    outb(0x60, data);
    return true;
}

static bool ps2_read_data(u8* data)
{
    if (!ps2_wait_output_full())
        return false;

    *data = inb(0x60);
    return true;
}

static bool ps2_write_config(u8 config)
{
    if (!ps2_command(0x60))
        return false;

    return ps2_write_data(config);
}

bool ps2_controller_init()
{
    /* Disable the keyboard and auxiliary PS/2 ports temporarily. */
    if (!ps2_command(0xAD) || !ps2_command(0xA7))
        return false;

    /* Drain any pending controller output. */
    for (u32 i = 0; i < 32 && (inb(0x64) & 1); ++i)
        (void)inb(0x60);

    /* Read the controller configuration byte. */
    if (!ps2_command(0x20))
        return false;

    u8 config;
    if (!ps2_read_data(&config))
        return false;

    /* Keep IRQ1 disabled until initialization is complete. */
    config &= ~(1u << 0);

    /* Enable the first-port clock and Set 2 -> Set 1 translation. */
    config &= ~(1u << 4);
    config |=  (1u << 6);

    if (!ps2_write_config(config))
        return false;

    /* Re-enable the keyboard port. */
    if (!ps2_command(0xAE))
        return false;

    /* Tell the keyboard to start scanning. */
    if (!ps2_write_data(0xF4))
        return false;

    u8 response;
    if (!ps2_read_data(&response) || response != 0xFA)
        return false; /* Expected ACK. */

    /* Now enable the keyboard's IRQ1 output. */
    config |= 1u << 0;

    if (!ps2_write_config(config))
        return false;

    return true;
}

char scancode_to_ascii(u8 scancode)
{
    static bool left_shift = false;
    static bool right_shift = false;
    static bool caps_lock = false;
    static bool caps_down = false;
    static bool num_lock = false;
    static bool num_down = false;
    static bool extended = false;
    static u8 pause_bytes = 0;

    if (pause_bytes) {
        --pause_bytes;
        return 0;
    }

    if (scancode == 0xE1) {
        pause_bytes = 5;
        extended = false;
        return 0;
    }

    if (scancode == 0xE0) {
        extended = true;
        return 0;
    }

    bool ext = extended;
    extended = false;

    bool released = (scancode & 0x80) != 0;
    u8 code = scancode & 0x7F;

    if (ext) {
        if (!released && code == 0x1C) return '\n';
        if (!released && code == 0x35) return '/';
        return 0;
    }

    switch (code) {
    case 0x2A:
        left_shift = !released;
        return 0;

    case 0x36:
        right_shift = !released;
        return 0;

    case 0x3A:
        if (released)
            caps_down = false;
        else if (!caps_down) {
            caps_lock = !caps_lock;
            caps_down = true;
        }
        return 0;

    case 0x45:
        if (released)
            num_down = false;
        else if (!num_down) {
            num_lock = !num_lock;
            num_down = true;
        }
        return 0;
    }

    if (released)
        return 0;

    bool shift = left_shift || right_shift;

    /* Keypad operators work independently of Num Lock. */
    if (code == 0x4A) return '-';
    if (code == 0x4E) return '+';

    /* Numeric keypad digits and decimal point. */
    if (code >= 0x47 && code <= 0x53) {
        char c = 0;

        switch (code) {
        case 0x47: c = '7'; break;
        case 0x48: c = '8'; break;
        case 0x49: c = '9'; break;
        case 0x4B: c = '4'; break;
        case 0x4C: c = '5'; break;
        case 0x4D: c = '6'; break;
        case 0x4F: c = '1'; break;
        case 0x50: c = '2'; break;
        case 0x51: c = '3'; break;
        case 0x52: c = '0'; break;
        case 0x53: c = '.'; break;
        }

        return ((num_lock ^ shift) && c) ? c : 0;
    }

    return lookup_scancode(code, shift, caps_lock);
}


extern "C" void keyboard_handler() {
    if (!set) {
        globalkeyboard = new GlobalKeyboard;
        set = true;
    }

    u8 status = inb(0x64);

    if (status & 1) {
        u8 scancode = inb(0x60);
        if (scancode == 0x57) {
            AcpiPM *acpi = (AcpiPM *)acpiadd;
            acpi->AcpiReset();
        }
            
        globalkeyboard->keyboards[0]->buffer[globalkeyboard->keyboards[0]->scancode_top] = scancode;
        globalkeyboard->keyboards[0]->scancode_top++;
        char ascii = scancode_to_ascii(scancode);
        if (ascii != 0 && !(scancode & 0x80)) {
            globalkeyboard->keyboards[0]->ascii[globalkeyboard->keyboards[0]->ascii_top] = ascii;
            globalkeyboard->keyboards[0]->ascii_top++;
        }
        if (globalkeyboard->keyboards[0]->scancode_top == 256)
            globalkeyboard->keyboards[0]->scancode_top = 0;
        if (globalkeyboard->keyboards[0]->ascii_top == 256)
            globalkeyboard->keyboards[0]->ascii_top = 0;
        keyboard_irq_count++;
    }
    
}