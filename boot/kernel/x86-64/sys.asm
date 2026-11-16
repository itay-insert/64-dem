BITS 64

global get_syscall
global enter_userspace

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

extern clock

extern sleep_ms

section .text

get_syscall:
lea rax, [rel syscall_entry]
ret

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



syscall_entry:

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
call clock
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


exit:

pop r11
pop rcx

mov [rel kernel_stack], rsp
mov rsp, [rel current_user_rsp]
o64 sysret
