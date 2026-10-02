#include "uint_definitions.h"

volatile char* vga_memory = (volatile char*)(0xB8000);

void putc(char ch, char color, int pos) {
    vga_memory[pos*2] = ch;
    vga_memory[pos*2+1] = color;
}

void main() {
    putc('P', 0x07, 0); // p for protected mode
    while (1);
}