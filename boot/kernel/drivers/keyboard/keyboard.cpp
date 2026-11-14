#include "uint_definitions.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "drivers/keyboard/hid.hpp"
#include "drivers/acpiPM.hpp"

extern u64 acpiadd;
extern GlobalKeyboard *globalkeyboard;

GlobalKeyboard::GlobalKeyboard() {
    keyboards[0] = new PS2K;
    keyboards[0]->init();

    keyboards[1] = new HIDK;
    keyboards[1]->init();
    
}

volatile u64 keyboard_irq_count = 0;


void GlobalKeyboard::proccess_scancode(u8 scancode) {
    AcpiPM *Acpi = (AcpiPM *)acpiadd;
    
    if (scancode == 0x57) 
        Acpi->AcpiReset();
    else if (scancode == 0x58)
        Acpi->AcpiShutDown();

    
    buffer[scancode_top] = scancode;
    scancode_top++;
    if (scancode_top == 256)
        scancode_top = 0;

   
    keyboard_irq_count++;   
}


void GlobalKeyboard::proccess_ascii(char ch) {
    ascii[ascii_top] = ch;
    ascii_top++;
    if (ascii_top == 256)
        ascii_top = 0;
}

extern "C" void interrupt_idle();

extern "C" u8 read_key(bool ascii, bool wait) {
    if (!globalkeyboard)
        return 0;

    if (ascii) {
        ascii_again:

        if (globalkeyboard->former_ascii != globalkeyboard->ascii_top) {
            u8 key = globalkeyboard->ascii[globalkeyboard->former_ascii];
            globalkeyboard->former_ascii = (globalkeyboard->former_ascii + 1) % 256;
            return key;
        } else if (wait) {
            while (globalkeyboard->former_ascii == globalkeyboard->ascii_top) 
                interrupt_idle();
            goto ascii_again;
        }
    } else if (!ascii) {
        scancode_again:

        if (globalkeyboard->former_scancode != globalkeyboard->scancode_top) {
            u8 key = globalkeyboard->buffer[globalkeyboard->former_scancode];
            globalkeyboard->former_scancode = (globalkeyboard->former_scancode + 1) % 256;
            return key;
        } else if (wait) {
            while (globalkeyboard->former_scancode == globalkeyboard->scancode_top)
                interrupt_idle();
            goto scancode_again;
        }
    }
}