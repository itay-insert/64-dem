BITS 32
global _start
extern main

section .text.start


_start:
mov ax, 0x10
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax

mov esp, 0x10000

sub esp, 4
jmp main