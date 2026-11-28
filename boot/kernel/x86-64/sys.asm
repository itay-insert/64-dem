BITS 64

global get_syscall
global enter_userspace
global resume_userspace

extern save_registers
extern restore_registers
extern SaveProccessRegs

extern current_proccess
extern render_rights

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

call restore_registers

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
call save_registers
call SaveProccessRegs
call fs_start
jmp exit

save_sysnum dq 0


exit_vga_stub:
pop rax
jmp vga_exit


vga_dispatcher:
sub rsp, 8
mov dword [rel save_sysnum + 4], 0 ; only low 32 bits


push rax
mov eax, dword [rel current_proccess]
cmp eax, dword [rel render_rights]
jne exit_vga_stub
pop rax

; kprintf
cmp qword [rel save_sysnum], 0
jne skip_kprintf
call kprintf
jmp vga_exit
skip_kprintf:


; mem_print
cmp qword [rel save_sysnum], 1
jne skip_mem
call print_physical_memory_stats
call print_proccess_mem
jmp vga_exit
skip_mem:


; ls
cmp qword [rel save_sysnum], 2
jne skip_ls
call ls
jmp vga_exit
skip_ls:


; backspace
cmp qword [rel save_sysnum], 3
jne skip_backspace
call backspace
jmp vga_exit
skip_backspace:


; print_drives()
cmp qword [rel save_sysnum], 4
jne skip_print_drives
call print_drives
jmp vga_exit
skip_print_drives:


; sys_begin()
cmp qword [rel save_sysnum], 5
jne skip_sysbegin
call set_begin
jmp vga_exit
skip_sysbegin:


cmp qword [rel save_sysnum], 6
jne skip_write ; sys_write to stdout
call write
jmp vga_exit
skip_write:


cmp qword [rel save_sysnum], 7
jne skip_getshell
call getshellsize ; get rows and coulmns
jmp vga_exit
skip_getshell:


cmp qword [rel save_sysnum], 8
jne skip_cursor_setpos
call cursor_Setpos ; syscall to set up the cursor position
jmp vga_exit
skip_cursor_setpos:


cmp qword [rel save_sysnum], 9
jne skip_move_cursor
call move_cursor ; syscall to move the cursor
jmp vga_exit
skip_move_cursor:


cmp qword [rel save_sysnum], 10
jne skip_Getpos
call Cursor_Getpos ; syscall to get current cursor row and coulmn as a single 64-bit qword
jmp vga_exit
skip_Getpos:


cmp qword [rel save_sysnum], 11
jne skip_clear
call clear_screen ; syscall to clear screen
jmp vga_exit
skip_clear:


cmp qword [rel save_sysnum], 12
jne skip_memcons
call cpy_memcons ; syscall to initialize and get the memory console buffer
jmp vga_exit
skip_memcons:


cmp qword [rel save_sysnum], 13
jne skip_memcons_size
call get_memcons_size ; syscall to get memcons_size
jmp vga_exit
skip_memcons_size:


cmp qword [rel save_sysnum], 14
jne skip_draw_cursor ; syscall to draw_cursor
call draw_cursor
jmp vga_exit
skip_draw_cursor:


cmp qword [rel save_sysnum], 15
jne skip_clear_row
call ClearRow  ; syscall to clear row
jmp vga_exit
skip_clear_row:


vga_exit:
add rsp, 8
ret


memory_dispatcher:
sub rsp, 8    
mov dword [rel save_sysnum + 4], 0 ; keep only low dword

cmp qword [rel save_sysnum], 0
jne skip_mem_ser
call memory_services
jmp memory_exit
skip_mem_ser:


cmp qword [rel save_sysnum], 1
jne skip_realloc
call realloc
jmp memory_exit
skip_realloc:

memory_exit:
add rsp, 8
ret


time_dispatcher:
sub rsp, 8
mov dword [rel save_sysnum + 4], 0 ; keep only low dword

cmp qword [rel save_sysnum], 0
jne skip_sleep
call sleep_ms
jmp time_exit
skip_sleep:


cmp qword [rel save_sysnum], 1
jne skip_date
call get_dateAndTime
jmp time_exit
skip_date:



cmp qword [rel save_sysnum], 2
jne skip_readfreq
call read_freq
jmp time_exit
skip_readfreq:


time_exit:
add rsp, 8
ret



fs_dispatcher:
sub rsp, 8
mov dword [rel save_sysnum + 4], 0


; fs_open()
cmp qword [rel save_sysnum], 0
jne skip_fs_open
call fs_open
jmp fs_exit
skip_fs_open:


; fs_read()
cmp qword [rel save_sysnum], 1
jne skip_fs_read
call fs_read
jmp fs_exit
skip_fs_read:


; fs_write()
cmp qword [rel save_sysnum], 2
jne skip_fs_write
call fs_write
jmp fs_exit
skip_fs_write:



; fs_seek()
cmp qword [rel save_sysnum], 3
jne skip_fs_seek
call fs_seek
jmp fs_exit
skip_fs_seek:


; fs_close()
cmp qword [rel save_sysnum], 4
jne skip_fs_close
call fs_close
jmp fs_exit
skip_fs_close:



; fs_start()
cmp qword [rel save_sysnum], 5
jne skip_fs_start
add rsp, 16 ; discard dispatcher padding and return address
jmp _start_syscall
skip_fs_start:



; get stat of file
cmp qword [rel save_sysnum], 6
jne skip_fs_stat
call stat
jmp fs_exit
skip_fs_stat:


fs_exit:
add rsp, 8
ret


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


mov qword [rel save_sysnum], rax

shr rax, 32

cmp rax, 0
jne skip_vga
call vga_dispatcher
jmp exit
skip_vga:


cmp rax, 1
jne skip_get_key
call read_key  ; key services
jmp exit
skip_get_key:


cmp rax, 2
jne skip_memory  ; sys_memory
call memory_dispatcher
jmp exit
skip_memory:


cmp rax, 3
jne skip_time ; timer and clock services
call time_dispatcher
jmp exit
skip_time:

cmp rax, 4 ; sys_exit
jne skip_sysexit
call sys_exit
skip_sysexit:


cmp rax, 5 ; acpi services
jne skip_acpi
call acpi_services
jmp exit
skip_acpi:


cmp rax, 6 ; filesystem services
jne skip_fs
call fs_dispatcher
jmp exit
skip_fs:

cmp rax, 7
jne skip_beep   ; sys_beep
call beep
jmp exit
skip_beep:


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
sti ; re-enable interrupts
o64 sysret
