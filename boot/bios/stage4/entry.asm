BITS 32
global _start
extern main

section .text.start


_start:
cli
mov ax, 0x10
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax

mov esp, 0x10000

; The gate offsets are filled at runtime because ELF relocation cannot split a
; handler address into the two 16-bit fields of an IDT gate.
mov eax, divide_error_stub
mov [idt + 0], ax
shr eax, 16
mov [idt + 6], ax

mov eax, nmi_stub
mov [idt + 16], ax
shr eax, 16
mov [idt + 22], ax

lidt [idt_descriptor]

sub esp, 4
jmp main

divide_error_stub:
    cli
.halt:
    hlt
    jmp .halt

nmi_stub:
    iretd

section .data
align 8
idt:
    dw 0                ; vector 0: divide error, offset low
    dw 0x08             ; stage 4 code selector
    db 0
    db 0x8E             ; present, ring 0, 32-bit interrupt gate
    dw 0                ; offset high
    dq 0                ; vector 1 is unused
    dw 0                ; vector 2: NMI, offset low
    dw 0x08
    db 0
    db 0x8E
    dw 0                ; offset high
idt_end:

idt_descriptor:
    dw idt_end - idt - 1
    dd idt
