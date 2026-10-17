#include "uint_definitions.h"
#include "x86-64/ports.h"

void speaker_on(unsigned hz) {
    if (hz < 20 || hz > 20000) return;

    u16 divisor = 1193182u / hz;
    outb(0x43, 0xB6);
    outb(0x42, divisor & 0xFF);
    outb(0x42, divisor >> 8);
    outb(0x61, inb(0x61) | 0x03);
}

void speaker_off() {
    outb(0x61, inb(0x61) & ~0x03);
}