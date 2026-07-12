BITS 64

extern APIC_base
extern page_fault
global isr_eoi
global isr_no_error
global isr_error
global page_fault_handler

section .text

isr_eoi:
push rax
mov rax, [rel APIC_base]
add rax, 0xb0
mov dword [rax], 0
pop rax
iretq


isr_no_error:
iretq


isr_error:
add rsp, 8
iretq

page_fault_handler:
cli
mov rax, cr2
mov rdi, rax
mov rsi, [rsp]
call page_fault
mov al, 0xB6
out 0x43, al

; 1000 Hz
mov ax, 1193
out 0x42, al
mov al, ah
out 0x42, al

; Enable speaker
in  al, 0x61
or  al, 3
out 0x61, al

halt:
hlt
jmp halt
