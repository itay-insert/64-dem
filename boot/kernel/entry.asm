BITS 64
global _start
extern kernel_main

section .text

_start:
mov rsp, rdx
sub rsp, 8
jmp kernel_main