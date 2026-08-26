BITS 32
global _start
extern main

section .text.start


_start:
sub esp, 4
jmp main