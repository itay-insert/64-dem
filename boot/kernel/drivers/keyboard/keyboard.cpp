#include "uint_definitions.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "drivers/keyboard/hid.hpp"
#include "drivers/acpiPM.hpp"

extern u64 acpiadd;

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

