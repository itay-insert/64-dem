BITS 64
global _start
extern main
section .text.start


_start:
mov [rel arg], rsi
call main
mov rax, 4
syscall


arg dq 0