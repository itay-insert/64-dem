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
global fs_open
global fs_read
global fs_write
global fs_seek
global fs_close
global fs_start
global print_drives
global beep
global set_begin
global read_freq
global stdout_write

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

fs_open:
    mov rax, 11
    syscall
    ret

fs_read:
    mov rax, 12
    syscall
    ret

fs_write:
    mov rax, 13
    syscall
    ret

fs_seek:
    mov rax, 14
    syscall
    ret

fs_close:
    mov rax, 15
    syscall
    ret

fs_start:
    mov rax, 16
    syscall
    ret

print_drives:
    mov rax, 17
    syscall
    ret

beep:
    mov rax, 18
    syscall
    ret

set_begin:
    mov rax, 19
    syscall
    ret

read_freq:
    mov rax, 20
    syscall
    ret

stdout_write:
    mov rax, 21
    syscall
    ret