BITS 64

section .text

global kprintf
global read_key
global malloc
global free
global realloc
global sys_exit
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
global fs_stat
global fs_start
global print_drives
global beep
global set_begin
global read_freq
global stdout_write
global getshellsize
global cursor_Setpos
global move_cursor
global Cursor_Getpos
global clear_screen
global get_memcons
global get_memcons_size
global draw_cursor
global clear_row


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

sys_exit:
    mov rax, 4
    syscall

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

getshellsize:
    mov rax, 22
    syscall
    ret

fs_stat:
    mov rax, 23
    syscall
    ret

cursor_Setpos:
    mov rax, 24
    syscall
    ret

move_cursor:
    mov rax, 25
    syscall
    ret

Cursor_Getpos:
    mov rax, 26
    syscall
    ret

clear_screen:
    mov rax, 27
    syscall
    ret

get_memcons:
    mov rax, 28
    syscall
    ret

get_memcons_size:
    mov rax, 29
    syscall
    ret

draw_cursor:
    mov rax, 30
    syscall
    ret

clear_row:
    mov rax, 32
    syscall
    ret