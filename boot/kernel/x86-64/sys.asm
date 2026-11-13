BITS 64

global get_syscall
global enter_userspace

extern kernel_stack
extern current_user_rsp
extern kprintf

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
push qword 0x40000028 ; rsp

iretq



syscall_entry:

mov [rel current_user_rsp], rsp       ; Save user RSP
mov rsp, [rel kernel_stack]

and rsp, -16

push rcx ; preserve syscall return state
push r11

call kprintf


pop r11
pop rcx

mov [rel kernel_stack], rsp
mov rsp, [rel current_user_rsp]
o64 sysret
