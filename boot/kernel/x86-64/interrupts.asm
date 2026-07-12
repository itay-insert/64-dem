BITS 64

extern APIC_base
extern exception_handler

global isr_eoi
global exception_stub_table

section .text

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


    ; 15 saved registers occupy 120 bytes above the normalized frame.
    mov rdi, [rsp + 120]       ; vector
    mov rsi, [rsp + 128]       ; error code
    mov rdx, [rsp + 136]       ; saved RIP
    mov rcx, [rsp + 144]       ; saved CS
    mov r8,  [rsp + 152]       ; saved RFLAGS
    mov r9, cr2                ; meaningful for #PF, harmless otherwise

    ; An interrupt can occur at either stack alignment.  Satisfy the SysV ABI
    ; explicitly before entering compiler-generated C code.
    and rsp, -16
    call exception_handler

.halt:
    hlt
    jmp .halt

section .rodata
align 8
exception_stub_table:
%assign i 0
%rep 32
    dq exception_stub_%+i
%assign i i+1
%endrep
