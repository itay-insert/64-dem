BITS 64
global _start
extern kernel_main

section .text

_start:
mov rsp, rdx
jmp kernel_main