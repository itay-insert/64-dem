[BITS 32]
global long_mode
extern PML4
extern g_buff
extern g_buff64
extern gstack
extern GMMP
extern kernel_entry

section .text

long_mode:
lgdt [gdt_descriptor]
mov eax, [PML4]
mov cr3, eax ; put the address of PML4 in cr3

mov eax, cr4
or eax, 1 << 5
mov cr4, eax

mov ecx, 0xC0000080 ; enable long mode
rdmsr
or eax, 1 << 8
wrmsr

mov eax, cr0 ; enable paging
or eax, 1 << 31
mov cr0, eax

jmp 0x08:long_mode_entry

gdt_start:
    dq 0x0000000000000000    ; 0x00: Null descriptor
    dq 0x00AF9A000000FFFF    ; 0x08: 64-bit code segment
    dq 0x00CF92000000FFFF    ; 0x10: Data segment

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start


[BITS 64]
long_mode_entry:
mov ax, 0x10 ; initializing segment registers
mov ds, ax
mov es, ax
mov ss, ax
mov fs, ax
mov gs, ax

mov rsp, 0x10000
call idt_init
mov rdi, [g_buff64]
mov rsi, [g_buff]
mov rdx, [gstack]
mov rcx, [GMMP]
mov rax, [kernel_entry]

jmp rax

%define IDT_ENTRIES     256
%define IDT_ENTRY_SIZE  16
%define KERNEL_CS       0x08
%define INTERRUPT_GATE  0x8E


%macro SET_IDT_GATE 2
    lea rax, [rel %2]
    lea rdi, [rel idt]
    add rdi, (IDT_ENTRY_SIZE * %1)

    mov word [rdi], ax
    mov word [rdi + 2], KERNEL_CS
    mov byte [rdi + 4], 0
    mov byte [rdi + 5], INTERRUPT_GATE

    mov rdx, rax
    shr rdx, 16
    mov word [rdi + 6], dx

    mov rdx, rax
    shr rdx, 32
    mov dword [rdi + 8], edx

    mov dword [rdi + 12], 0
%endmacro


idt_init:
    ; Fill all 256 entries with the fallback handler.
    lea rax, [rel isr_unhandled]

    mov r8, rax
    mov r9, rax
    shr r9, 16

    mov r10, rax
    shr r10, 32

    lea rdi, [rel idt]
    mov ecx, IDT_ENTRIES

.fill_idt:
    ; Bytes 0-1: handler address bits 15:0
    mov ax, r8w
    mov word [rdi], ax

    ; Bytes 2-3: code segment selector
    mov word [rdi + 2], KERNEL_CS

    ; Byte 4: IST index = 0
    mov byte [rdi + 4], 0

    ; Byte 5: present, ring 0, interrupt gate
    mov byte [rdi + 5], INTERRUPT_GATE

    ; Bytes 6-7: handler address bits 31:16
    mov ax, r9w
    mov word [rdi + 6], ax

    ; Bytes 8-11: handler address bits 63:32
    mov dword [rdi + 8], r10d

    ; Bytes 12-15: reserved
    mov dword [rdi + 12], 0

    add rdi, IDT_ENTRY_SIZE
    loop .fill_idt

    ; Replace vector 0 with the divide-error handler.
    SET_IDT_GATE 0, isr_divide_error

    ; Replace vector 2 with the NMI handler.
    SET_IDT_GATE 2, isr_nmi

    ; Build the 10-byte IDTR on the current stack.
    ; Limit = 256 * 16 - 1 = 4095.
    sub rsp, 16

    mov word [rsp], (IDT_ENTRIES * IDT_ENTRY_SIZE) - 1

    lea rax, [rel idt]
    mov qword [rsp + 2], rax

    lidt [rsp]

    add rsp, 16
    ret


; ============================================================
; SET_IDT_GATE vector, handler
;
; Writes one 16-byte 64-bit interrupt gate.
; ============================================================


; ============================================================
; Divide Error (#DE), vector 0
;
; Halt permanently for debugging.
; Do NOT return to the faulting division unchanged.
; ============================================================

isr_divide_error:
    cli

.divide_halt:
    hlt
    jmp .divide_halt


; ============================================================
; Non-Maskable Interrupt (NMI), vector 2
;
; Do nothing and return to the interrupted code.
; NMI pushes no error code.
; ============================================================

isr_nmi:
    iretq

isr_unhandled:
    cli

.unhandled_halt:
    hlt
    jmp .unhandled_halt


; ============================================================
; IDT storage: 256 entries * 16 bytes = 4096 bytes
; ============================================================

section .bss
align 16

idt:
    resb IDT_ENTRIES * IDT_ENTRY_SIZE