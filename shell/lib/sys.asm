BITS 64

section .text

global kprintf
global read_key
global malloc
global free
global realloc
global sleep_ms
global get_dateAndTime
global print_mem
global acpi
global ls
global backspace

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

get_dateAndTime:
    mov rax, 6
    syscall
    ret


print_mem:
    mov rax, 7
    syscall
    ret

acpi:
    mov rax, 8
    syscall
    ret

ls:
    mov rax, 9
    syscall
    ret

backspace:
    mov rax, 10
    syscall
    ret