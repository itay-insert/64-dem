BITS 64
global _start
global arg
extern main
section .text.start


_start:
mov [rel arg], rsi
call main
mov rax, 0x400000000
syscall


arg dq 0