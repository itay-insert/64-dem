BITS 64

section .text

global printf

printf:
    syscall
    ret