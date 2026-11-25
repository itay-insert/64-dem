BITS 64

global get_syscall
global enter_userspace
global resume_userspace

extern kernel_stack
extern current_user_rsp
extern kprintf
extern read_key
extern memory_services
extern realloc
extern get_dateAndTime
extern print_physical_memory_stats
extern acpi_services
extern ls
extern backspace
extern fs_open
extern fs_read
extern fs_write
extern fs_seek
extern fs_close
extern stat
extern fs_start
extern former_user_rsp
extern print_proccess_mem
extern print_drives
extern beep
extern set_begin
extern read_freq
extern write
extern getshellsize
extern cursor_Setpos
extern move_cursor
extern Cursor_Getpos
extern clear_screen
extern cpy_memcons
extern get_memcons_size
extern draw_cursor
extern ClearRow

extern sys_exit

extern sleep_ms

section .text

get_syscall:
lea rax, [rel syscall_entry]
ret

rflags dq 0
rip dq 0


enter_userspace:
mov ax, 0x1B ; set user data segment
mov ds, ax
mov es, ax

push qword 0x1B ; ss
push rdi ; user rsp
push qword 0x202 ; rflags
push qword 0x23 ;  CS
push qword 0x40000028 ; rip
iretq

save_rsp dq 0

resume_userspace:
mov ax, 0x1B
mov ds, ax
mov es, ax

mov qword [rel save_rsp], rsp
mov rsp, qword [rel former_user_rsp]

pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rbp
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
pop rax

mov qword [rel former_user_rsp], rsp
mov rsp, qword [rel save_rsp]

push qword 0x1B
push qword [rel former_user_rsp]
push qword [rel rflags]
push qword 0x23
push qword [rel rip]
iretq


_start_syscall:
mov rax, qword [rsp]
mov qword [rel rflags], rax

mov rax, qword [rsp + 8]
mov qword [rel rip], rax
call fs_start
jmp exit

syscall_entry:
push rax
push rbx
push rcx
push rdx
push rsi
push rdi
push rbp
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15

mov [rel current_user_rsp], rsp       ; Save user RSP
mov rsp, [rel kernel_stack]

and rsp, -16

push rcx ; preserve syscall return state
push r11



sti ; enable interrupts after jumping to handler disabled

cmp rax, 0
jne skip_kprintf
call kprintf    ; sys_print, rax = 0
jmp exit
skip_kprintf:

cmp rax, 1
jne skip_get_key
call read_key
jmp exit
skip_get_key:

cmp rax, 2 ; memory services
jne skip_mem_ser
call memory_services
jmp exit
skip_mem_ser:

cmp rax, 3 ; realloc
jne skip_realloc
call realloc
jmp exit
skip_realloc:

cmp rax, 4 ; sys_exit
jne skip_sysexit
call sys_exit
skip_sysexit:

cmp rax, 5 ; sleep_ms
jne skip_sleep
call sleep_ms
jmp exit
skip_sleep:

cmp rax, 6 ; get date and time
jne skip_date
call get_dateAndTime
jmp exit
skip_date:


cmp rax, 7 ; print memory status
jne skip_mem
call print_physical_memory_stats
call print_proccess_mem
jmp exit
skip_mem:

cmp rax, 8 ; acpi
jne skip_acpi
call acpi_services
jmp exit
skip_acpi:


cmp rax, 9 ; ls
jne skip_ls
call ls
jmp exit
skip_ls:


cmp rax, 10
jne skip_backspace  ; backspace
call backspace
jmp exit
skip_backspace:

cmp rax, 11
jne skip_fs_open ; sys_fs_open()
call fs_open
jmp exit
skip_fs_open:

cmp rax, 12
jne skip_fs_read ; sys fs_read()
call fs_read
jmp exit
skip_fs_read:

cmp rax, 13
jne skip_fs_write ; sys fs_write()
call fs_write
jmp exit
skip_fs_write:

cmp rax, 14
jne skip_fs_seek ; sys fs_seek()
call fs_seek
jmp exit
skip_fs_seek:

cmp rax, 15
jne skip_fs_close ; sys fs_close()
call fs_close
jmp exit
skip_fs_close:


cmp rax, 16
jne skip_fs_start
jmp _start_syscall  ; sys fs_start()
jmp exit
skip_fs_start:


cmp rax, 17
jne skip_print_drives ; sys print_drives()
call print_drives
jmp exit
skip_print_drives:


cmp rax, 18
jne skip_beep   ; sys_beep
call beep
jmp exit
skip_beep:


cmp rax, 19
jne skip_sysbegin ; sys_begin
call set_begin
jmp exit
skip_sysbegin:


cmp rax, 20
jne skip_readfreq ; sys read_freq
call read_freq
jmp exit
skip_readfreq:


cmp rax, 21
jne skip_write ; sys_write to stdout
call write
jmp exit
skip_write:

cmp rax, 22
jne skip_getshell
call getshellsize ; get rows and coulmns
jmp exit
skip_getshell:

cmp rax, 23
jne skip_fs_stat ; get stat of file
call stat
jmp exit
skip_fs_stat:


cmp rax, 24
jne skip_cursor_setpos
call cursor_Setpos ; syscall to set up the cursor position
jmp exit
skip_cursor_setpos:


cmp rax, 25
jne skip_move_cursor
call move_cursor ; syscall to move the cursor
jmp exit
skip_move_cursor:


cmp rax, 26
jne skip_Getpos
call Cursor_Getpos ; syscall to get current cursor row and coulmn as a single 64-bit qword
jmp exit
skip_Getpos:

    
cmp rax, 27
jne skip_clear
call clear_screen ; syscall to clear screen
jmp exit
skip_clear:


cmp rax, 28
jne skip_memcons
call cpy_memcons ; syscall to initialize and get the memory console buffer
jmp exit
skip_memcons:


cmp rax, 29
jne skip_memcons_size
call get_memcons_size ; syscall to get memcons_size
jmp exit
skip_memcons_size:


cmp rax, 30
jne skip_draw_cursor ; syscall to draw_cursor
call draw_cursor
jmp exit
skip_draw_cursor:


cmp rax, 32
jne skip_clear_row
call ClearRow  ; syscall to clear row
jmp exit
skip_clear_row:

exit:

pop r11
pop rcx

mov [rel kernel_stack], rsp
mov rsp, [rel current_user_rsp]

pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rbp
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
add rsp, 8
o64 sysret
