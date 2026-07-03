BITS 64

global stack_get

section .text

stack_get:
mov rax, rsp
add rax, 8
ret