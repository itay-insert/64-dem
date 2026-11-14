BITS 64

extern APIC_base
extern exception_handler
extern timer_handler

global spurious_stub
global isr_eoi
global exception_stub_table
global timer_stub

extern former_ss
extern former_cs
extern former_rflags
extern former_rsp
extern former_rip

isr_timer:
push rax
push rbx
push rcx
push rdx
push rbp
push rdi
push rsi
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15
cld

mov rbp, rsp
and rsp, -16
; The USB worker may use x87/SSE. Preserve the interrupted state as well as GPRs.
sub rsp, 512
fxsave64 [rsp]
mov rdi, [rbp + 128] ; interrupted CS
mov rsi, [rbp + 120] ; interrupted RIP
call timer_handler
fxrstor64 [rsp]
mov rsp, rbp

pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rsi
pop rdi
pop rbp
pop rdx
pop rcx
pop rbx
pop rax
iretq

isr_spurious:
iretq

; Timer work is safe when interrupted at this explicit kernel idle point.
global interrupt_idle
global interrupt_idle_resume
interrupt_idle:
    sti
    hlt
interrupt_idle_resume:
    ret

isr_eoi:
    push rax
    mov rax, [rel APIC_base]
    add rax, 0xb0
    mov dword [rax], 0
    pop rax
    iretq

; Exceptions without a hardware error code get a synthetic zero.  Every stub
; therefore enters common_exception with [rsp] = vector and [rsp+8] = error.
%macro ISR_NO_ERROR 1
global exception_stub_%1
exception_stub_%1:
    push qword 0
    push qword %1
    jmp common_exception
%endmacro

%macro ISR_ERROR 1
global exception_stub_%1
exception_stub_%1:
    push qword %1
    jmp common_exception
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR    8
ISR_NO_ERROR 9
ISR_ERROR    10
ISR_ERROR    11
ISR_ERROR    12
ISR_ERROR    13
ISR_ERROR    14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR    17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR    21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR    29
ISR_ERROR    30
ISR_NO_ERROR 31

common_exception:
    cli

    ; Preserve the interrupted integer state before diagnostics clobber it.
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15


    cld
    ; 15 saved registers occupy 120 bytes above the normalized frame.
    mov rdi, [rsp + 120]       ; vector
    mov rsi, [rsp + 128]       ; error code
    mov rdx, [rsp + 136]       ; saved RIP
    mov rcx, [rsp + 144]       ; saved CS
    mov r8,  [rsp + 152]       ; saved RFLAGS
    mov r9, cr2                ; meaningful for #PF, harmless otherwise
    
    and rsp, -16

    
    call exception_handler

.halt:
    hlt
    jmp .halt

exception_beep:
   
    mov al, 0xb6
    out 0x43, al

   
    mov ax, 1356
    out 0x42, al
    mov al, ah
    out 0x42, al

   
    in al, 0x61
    mov ah, al
    or al, 0x03
    out 0x61, al

    
    mov ecx, 10000000
.delay:
    pause
    loop .delay

   
    out 0x61, al
    ret

section .rodata
align 8
exception_stub_table:
%assign i 0
%rep 32
    ; Store a link/load-address-independent signed offset.  idt.c adds the
    ; runtime address of this table before installing the gate.
    dq exception_stub_%+i - exception_stub_table
%assign i i+1
%endrep

section .rodata
align 8
spurious_stub:
dq isr_spurious - spurious_stub

section .rodata
align 8
timer_stub:
dq isr_timer - timer_stub


extern keyboard_handler

global isr_keyboard
global keyboard_stub

section .text

isr_keyboard:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld

    ; Preserve the saved-register stack position.
    ; Align RSP for the System V AMD64 C calling convention.
    mov rbp, rsp
    and rsp, -16
    call keyboard_handler
    mov rsp, rbp

    ; Acknowledge the interrupt at the Local APIC.
    mov rax, [rel APIC_base]
    mov dword [rax + 0xB0], 0

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax

    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    mov rdi, [rsp + 40] ; rip 
    mov rsi, [rsp + 48] ; cs
    mov rdx, [rsp + 56] ; rflags
    mov rcx, [rsp + 64] ; rsp
    mov r8, [rsp + 72] ; ss

    mov qword [rel former_ss], r8
    mov qword [rel former_cs], rsi
    mov qword [rel former_rflags], rdx
    mov qword [rel former_rsp], rcx
    mov qword [rel former_rip], rdi

    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    iretq

section .rodata
align 8

keyboard_stub:
    dq isr_keyboard - keyboard_stub

; Device vectors 0x50..0x7f. No error code is pushed for external IRQs.
extern device_irq_dispatch
global device_irq_stub_table
section .text
%assign irq_vector 0x50
%rep 48
device_irq_%+irq_vector:
    push qword irq_vector
    jmp device_irq_common
%assign irq_vector irq_vector+1
%endrep

device_irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, [rsp + 120]
    mov rbp, rsp
    and rsp, -16
    call device_irq_dispatch
    mov rsp, rbp
    mov rax, [rel APIC_base]
    mov dword [rax + 0xB0], 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 8
    iretq

section .rodata
align 8
device_irq_stub_table:
%assign irq_vector 0x50
%rep 48
    dq device_irq_%+irq_vector - device_irq_stub_table
%assign irq_vector irq_vector+1
%endrep
