BITS 64
global _start
extern main
section .text.start

_start:
call main
mov rax, 4
syscall

