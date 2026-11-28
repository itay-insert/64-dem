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
    mov rax, 0x100000000 ; read key
    syscall
    ret

malloc:
    mov rax, 0x200000000 ; memory services
    mov rsi, rdi
    mov rdi, 0 ; select malloc
    syscall
    ret

free:
    mov rax, 0x200000000 ; memory services
    mov rsi, rdi
    mov rdi, 1 ; select free
    syscall
    ret

realloc:
    mov rax, 0x200000001 ; realloc
    syscall
    ret

sys_exit:
    mov rax, 0x400000000 ; sys_exit
    syscall

sleep_ms:
    mov rax, 0x300000000 ; sys_sleep_ms()
    syscall
    ret

get_dateAndTime:
    mov rax, 0x300000001 ; syscall to get current date and time
    syscall
    ret


print_mem:
    mov rax, 1 ; syscall to print mem
    syscall
    ret

acpi:
    mov rax, 0x500000000
    syscall
    ret

ls:
    mov rax, 2
    syscall
    ret

backspace:
    mov rax, 3
    syscall
    ret

fs_open:
    mov rax, 0x600000000 ; syscall to open a file
    syscall
    ret

fs_read:
    mov rax, 0x600000001 ; syscall to read file
    syscall
    ret

fs_write:
    mov rax, 0x600000002 ; syscall to write a file
    syscall
    ret

fs_seek:
    mov rax, 0x600000003 ; syscall to change file offset
    syscall
    ret

fs_close:
    mov rax, 0x600000004 ; syscall to close a file
    syscall
    ret

fs_start:
    mov rax, 0x600000005 ; syscall to start a program
    syscall
    ret

print_drives:
    mov rax, 4 ; syscall to print_drives()
    syscall
    ret

beep:
    mov rax, 0x700000000 ; sys_beep()
    syscall
    ret

set_begin:
    mov rax, 5 
    syscall
    ret

read_freq:
    mov rax, 0x300000003 ; syscall to read timer frequency
    syscall
    ret

stdout_write:
    mov rax, 6 ; syscall to write to stdout
    syscall
    ret

getshellsize:
    mov rax, 7 ; syscall to get shellsize
    syscall
    ret

fs_stat:
    mov rax, 0x600000006 ; syscall to get the stat of a file
    syscall
    ret

cursor_Setpos:
    mov rax, 8 ; syscall to set cursor position
    syscall
    ret

move_cursor:
    mov rax, 9 ; syscall to move cursor
    syscall
    ret

Cursor_Getpos:
    mov rax, 10 ; syscall to get position of cursor
    syscall
    ret

clear_screen:
    mov rax, 11 ; syscall to clear screen
    syscall
    ret

get_memcons:
    mov rax, 12
    syscall
    ret

get_memcons_size:
    mov rax, 13
    syscall
    ret

draw_cursor:
    mov rax, 14
    syscall
    ret

clear_row:
    mov rax, 15
    syscall
    ret