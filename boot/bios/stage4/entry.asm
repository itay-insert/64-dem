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

lidt [idt_descriptor] ; loading the idt to handle NMIs

push edx
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


gdt_start:
    dq 0                    ; null descriptor (0x00)

      ; 32-bit kernel code (0x08): base 0, limit 4 GiB
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10011010b            ; present, ring 0, code, readable
    db 11001111b            ; 4 KiB granularity, 32-bit, limit high = 0xF
    db 0x00

      ; 32-bit kernel data (0x10): base 0, limit 4 GiB
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b            ; present, ring 0, data, writable
    db 11001111b
    db 0x00

    ; 16-bit bridge code (0x18): stage 4 is loaded at physical 0x18000.
    dw 0xffff
    dw 0x8000
    db 0x01
    db 10011010b
    db 00000000b
    db 0x00

    ; 16-bit bridge data (0x20): covers stage 3 and stage 4 below 0x20000.
    dw 0xffff
    dw 0x0000
    db 0x01
    db 10010010b
    db 00000000b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .text.start
; u32 RealModeWrapper(bios_services *table, u32 service, u32 arg1,
;                     u32 arg2, u32 arg3, u32 *secondary);
; service 0: disk read (arg1=LBA, arg2=physical buffer, arg3=sectors),
;            returns 0 on success or 1 on BIOS error.
; service 1: cluster to LBA (arg1=cluster), returns LBA.
; service 2: LBA to cluster (arg1=LBA), returns cluster; secondary=remainder.
; service 3: sector count (arg1=sectors), returns clusters; secondary=remainder.
; service 4: next cluster (arg1=current cluster), returns next cluster.
; service 5: print (arg1=physical string address, arg2=length), returns 0.
; Invalid service/arguments return 0xffffffff. Pointers must be below 1 MiB.
global RealModeWrapper
RealModeWrapper:
    pushfd
    cli
    pushad

    mov eax, [esp + 40]       ; service table supplied to stage 4 in EDX
    cmp eax, 0x10000
    jb .invalid
    cmp eax, 0x20000
    jae .invalid
    mov ecx, [esp + 44]       ; service number
    cmp ecx, 5
    ja .invalid
    movzx edx, word [eax + ecx * 2]
    test edx, edx
    jz .invalid
    mov [service_far], dx
    mov dx, [eax + 12]       ; code_seg follows the six service offsets
    test dx, dx
    jz .invalid
    mov [service_far + 2], dx
    mov [service_number], cx

    mov eax, [esp + 48]
    mov [service_arg1], eax
    mov eax, [esp + 52]
    mov [service_arg2], eax
    mov eax, [esp + 56]
    mov [service_arg3], eax
    mov eax, [esp + 60]
    mov [secondary_ptr], eax

    cmp ecx, 0
    je .disk_args
    cmp ecx, 5
    je .print_args
    jmp .enter_real_mode

.disk_args:
    mov eax, [service_arg2]
    cmp eax, 0x100000
    jae .invalid
    mov edx, [service_arg3]
    test edx, edx
    jz .invalid
    cmp edx, 127             ; EDD read packet sector count limit
    ja .invalid
    shl edx, 9
    add eax, edx
    jc .invalid
    cmp eax, 0x100000       ; transfer must stay in real-mode memory
    ja .invalid
    jmp .enter_real_mode

.print_args:
    mov eax, [service_arg1]
    cmp eax, 0x100000
    jae .invalid
    mov eax, [service_arg2]
    cmp eax, 0xffff
    ja .invalid
    mov edx, [service_arg1]
    and edx, 0x000f
    add edx, eax
    cmp edx, 0x10000        ; DS:SI must stay within one segment
    ja .invalid
    add eax, [service_arg1]
    jc .invalid
    cmp eax, 0x100000
    ja .invalid
    cmp dword [service_arg2], 0 ; LOOP with CX=0 prints 65536 bytes
    jnz .enter_real_mode
    xor eax, eax
    jmp .finish

.enter_real_mode:
    mov [saved_esp], esp
    sidt [saved_idtr]
    lgdt [gdt_descriptor]
    lidt [real_mode_idtr]
    jmp far [pm16_jump_ptr]

.invalid:
    mov eax, 0xffffffff
.finish:
    mov [esp + 28], eax       ; saved EAX slot in PUSHAD frame
    popad
    popfd
    ret

BITS 16
pm16_to_real:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ebp, bridge_data - 0x18000
    mov eax, cr0
    and eax, ~1
    mov cr0, eax
    jmp dword far [cs:bp + real_jump_ptr - bridge_data]

real_mode_entry:
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    mov ax, 0x6000
    mov ss, ax
    mov sp, 0xfff0           ; real-mode stack at physical 0x6fff0
    mov ebp, bridge_data - 0x10000

    mov eax, [ds:bp + service_arg1 - bridge_data]
    mov si, [ds:bp + service_number - bridge_data]
    cmp si, 0
    je .disk
    cmp si, 5
    je .print
    jmp .call_service

.disk:
    mov edx, [ds:bp + service_arg2 - bridge_data]
    mov bx, dx
    and bx, 0x000f
    shr edx, 4
    mov di, dx               ; DI:BX is the BIOS DAP buffer address
    mov cx, [ds:bp + service_arg3 - bridge_data]
    jmp .call_service

.print:
    mov cx, [ds:bp + service_arg2 - bridge_data]
    mov edx, [ds:bp + service_arg1 - bridge_data]
    mov si, dx
    and si, 0x000f
    shr edx, 4
    mov ds, dx               ; DS:SI points to the caller's string

.call_service:
    mov ebp, bridge_data - 0x18000
    call far [cs:bp + service_far - bridge_data]
    mov bx, 0x1000
    mov ds, bx
    mov ebp, bridge_data - 0x10000
    cmp word [ds:bp + service_number - bridge_data], 5
    jne .save_result
    xor eax, eax
.save_result:
    mov [ds:bp + result_eax - bridge_data], eax
    mov [ds:bp + result_edx - bridge_data], edx
    cli
    lgdt [ds:bp + gdt_descriptor - bridge_data]
    mov ebp, bridge_data - 0x18000
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword far [cs:bp + pm16_return_ptr - bridge_data]

pm16_return:
    mov ax, 0x20
    mov ds, ax
    mov ax, 0x10
    mov ss, ax
    mov ebp, bridge_data - 0x10000
    mov esp, [ds:bp + saved_esp - bridge_data]
    jmp dword 0x08:pm32_return

BITS 32
pm32_return:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    lidt [saved_idtr]

    mov eax, [result_eax]
    mov edx, [secondary_ptr]
    test edx, edx
    jz .no_secondary
    mov ecx, [result_edx]
    mov [edx], ecx
.no_secondary:
    jmp RealModeWrapper.finish

section .data
align 4
bridge_data:
pm16_jump_ptr: dd pm16_to_real - 0x18000
               dw 0x18
real_jump_ptr: dd real_mode_entry - 0x18000
               dw 0x1800
pm16_return_ptr: dd pm16_return - 0x18000
                 dw 0x18
real_mode_idtr:
    dw 0x03ff
    dd 0
saved_idtr: times 6 db 0
saved_esp: dd 0
service_far: dw 0, 0x1000
service_number: dw 0
service_arg1: dd 0
service_arg2: dd 0
service_arg3: dd 0
secondary_ptr: dd 0
result_eax: dd 0
result_edx: dd 0

section .note.GNU-stack noalloc noexec nowrite progbits
