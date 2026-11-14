BITS 64

section .text

global kprintf
global read_key
global malloc
global free
global realloc
global sleep_ms

kprintf:
    xor rax, rax ; kprintf
    syscall
    ret


read_key:
    mov rax, 1 ; read key
    syscall
    ret

malloc:
    mov rax, 2 ; memory services
    mov rsi, rdi
    mov rdi, 0 ; select malloc
    syscall
    ret

free:
    mov rax, 2 ; memory services
    mov rsi, rdi
    mov rdi, 1 ; select free
    syscall
    ret

realloc:
    mov rax, 3 ; realloc
    syscall
    ret

sleep_ms:
    mov rax, 5
    syscall
    ret