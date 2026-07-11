BITS 64

extern APIC_base
global isr_eoi
global isr_no_error
global isr_error

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

