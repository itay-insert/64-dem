BITS 64
global _start
extern kernel_main

section .text

_start:
mov rsp, rdx
sub rsp, 8

; Earliest possible kernel marker for QEMU's isa-debugcon (port 0xE9).
mov r8, rdx
mov dx, 0xe9
mov al, 'E'
out dx, al
mov al, 10
out dx, al
mov rdx, r8

jmp kernel_main
