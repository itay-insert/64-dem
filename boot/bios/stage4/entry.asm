[BITS 32]
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
; u32 RealModeWrapper(bios_services *table, u32 service, u32 arg1,
;                     u32 arg2, u32 arg3, u32 *secondary);
; service 0: disk read (arg1=LBA, arg2=physical buffer, arg3=sectors),
;            returns 0 on success or 1 on BIOS error.
; service 1: cluster to LBA (arg1=cluster), returns LBA.
; service 2: LBA to cluster (arg1=LBA), returns cluster; secondary=remainder.
; service 3: sector count (arg1=sectors), returns clusters; secondary=remainder.
; service 4: next cluster (arg1=current cluster), returns next cluster.
; service 5: print (arg1=physical string address, arg2=length), returns 0.
; service 6: E820 map (arg1=physical address of 1024 entries), returns count
;            or 0xffffffff on BIOS error or map overflow.
; service 7: set VBE mode (arg1=physical address of 24-byte result),
;            returns 0 on success or 1 if no supported mode is available.
; Invalid service/arguments return 0xffffffff. Pointers must be below 1 MiB.

section .text

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
    cmp ecx, 7
    ja .invalid
    cmp ecx, 6
    je .local_map_service
    cmp ecx, 7
    je .local_vbe_service
    movzx edx, word [eax + ecx * 2]
    test edx, edx
    jz .invalid
    mov [service_far], dx
    mov dx, [eax + 12]       ; code_seg follows the six service offsets
    test dx, dx
    jz .invalid
    mov [service_far + 2], dx
    jmp .service_ready

.local_map_service:
    mov edx, get_memory_map
    sub edx, 0x18000
    mov [service_far], dx
    mov word [service_far + 2], 0x1800
    jmp .service_ready

.local_vbe_service:
    mov edx, vbe_init
    sub edx, 0x18000
    mov [service_far], dx
    mov word [service_far + 2], 0x1800

.service_ready:
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
    cmp ecx, 6
    je .memory_map_args
    cmp ecx, 7
    je .vbe_args
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

.memory_map_args:
    mov eax, [service_arg1]
    cmp eax, 0x10000
    jb .invalid
    add eax, 1024 * 24
    jc .invalid
    cmp eax, 0x100000
    ja .invalid
    jmp .enter_real_mode

.vbe_args:
    mov eax, [service_arg1]
    cmp eax, 0x10000
    jb .invalid
    cmp eax, 0x100000 - 24
    ja .invalid
    jmp .enter_real_mode

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

section .bridge16
[BITS 16]
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
    cmp si, 7
    je .vbe
    jmp .call_service

.disk:
    mov edx, [ds:bp + service_arg2 - bridge_data]
    mov bx, dx
    and bx, 0x000f
    shr edx, 4
    mov di, dx               ; DI:BX is the BIOS DAP buffer address
    mov cx, [ds:bp + service_arg3 - bridge_data]
    jmp .call_service

.vbe:
    mov edx, [ds:bp + service_arg1 - bridge_data]
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

; Called with CS=0x1800 and EAX=the physical destination address. Each
; E820 transfer uses a fresh segment:offset so the 24 KiB map can cross a
; 64 KiB boundary. The BIOS continuation token stays in EBX.
get_memory_map:
    mov ebp, bridge_data - 0x18000
    mov [cs:bp + e820_next - bridge_data], eax
    mov dword [cs:bp + e820_count - bridge_data], 0
    xor ebx, ebx
.next_entry:
    cmp dword [cs:bp + e820_count - bridge_data], 1024
    jae .error                 ; the BIOS still has another entry
    mov eax, [cs:bp + e820_next - bridge_data]
    mov di, ax
    and di, 0x000f
    shr eax, 4
    mov es, ax
    mov dword [es:di + 20], 1 ; request ACPI 3.x extended attributes
    mov eax, 0xe820
    mov edx, 0x534d4150      ; 'SMAP'
    mov ecx, 24
    int 0x15
    mov ebp, bridge_data - 0x18000
    jc .bios_end
    cmp eax, 0x534d4150
    jne .error
    cmp ecx, 20
    jb .error
    cmp ecx, 24
    jb .accept_entry         ; 20-byte BIOS entry has implicit valid bit
    mov eax, [cs:bp + e820_next - bridge_data]
    mov di, ax
    and di, 0x000f
    shr eax, 4
    mov es, ax
    test dword [es:di + 20], 1
    jz .continue
.accept_entry:
    add dword [cs:bp + e820_next - bridge_data], 24
    inc dword [cs:bp + e820_count - bridge_data]
.continue:
    test ebx, ebx
    jnz .next_entry
.done:
    mov eax, [cs:bp + e820_count - bridge_data]
    retf
.bios_end:
    cmp dword [cs:bp + e820_count - bridge_data], 0
    jne .done                ; some BIOSes end with CF on the next call
.error:
    mov eax, 0xffffffff
    retf


; ============================================================
; VBE output structure
; ============================================================

section .bridge_data16
align 16
vbe_mode:
    .pixel_mode:             dd 0 ; 0 = RGB, 1 = BGR (kernel convention)
    .horizontal_resolution:  dd 0
    .vertical_resolution:    dd 0
    .pixels_per_scanline:    dd 0
    .info_size:              dd 24
    .framebuffer_address:    dd 0


; ============================================================
; BIOS buffers
; ============================================================

vbe_controller:
    times 512 db 0

vbe_mode_info:
    times 256 db 0


; ============================================================
; Temporary variables
; ============================================================

vbe_best_mode:      dw 0
vbe_best_x:         dw 0
vbe_best_y:         dw 0
vbe_best_area:      dd 0

target_address: dd 0

vbe_current_x: dw 0
vbe_current_y: dw 0

section .bridge16
vbe_init:

    ; DS = 0x1000 when vbe_init is called.
    ; vbe_mode is the base of the VBE data block.
    mov ebp, vbe_mode - 0x10000

    mov [ds:bp + target_address - vbe_mode], edx

    push bx
    push cx
    push dx
    push si
    push di
    push es
    push fs


    ; --------------------------------------------------------
    ; Get VBE controller information
    ; --------------------------------------------------------

    mov ax, ds
    mov es, ax

    mov di, vbe_controller - vbe_mode
    add di, bp

    ; Request the VBE 2.0+ controller information block.
    mov word [es:di], 'VB'
    mov word [es:di+2], 'E2'

    mov ax, 4F00h
    int 10h

    cmp ax, 004Fh
    jne _failure


    ; --------------------------------------------------------
    ; Get pointer to VBE mode list
    ;
    ; controller + 0Eh = mode list offset
    ; controller + 10h = mode list segment
    ; --------------------------------------------------------

    mov si, [es:bp + vbe_controller - vbe_mode + 0Eh]

    mov ax, [es:bp + vbe_controller - vbe_mode + 10h]
    mov fs, ax


    ; --------------------------------------------------------
    ; No best mode yet
    ; --------------------------------------------------------

    mov word [ds:bp + vbe_best_mode - vbe_mode], 0
    mov word [ds:bp + vbe_best_x - vbe_mode], 0
    mov word [ds:bp + vbe_best_y - vbe_mode], 0
    mov dword [ds:bp + vbe_best_area - vbe_mode], 0


    ; ========================================================
    ; Walk VBE mode list
    ; ========================================================

_next_mode:

    mov bx, [fs:si]
    add si, 2

    cmp bx, 0FFFFh
    je _found_mode


    ; --------------------------------------------------------
    ; Get information about this mode
    ; --------------------------------------------------------

    mov ax, ds
    mov es, ax

    mov di, vbe_mode_info - vbe_mode
    add di, bp

    mov cx, bx
    mov ax, 4F01h
    int 10h

    cmp ax, 004Fh
    jne _next_mode


    ; --------------------------------------------------------
    ; Mode attributes
    ;
    ; bit 0  = mode supported
    ; bit 4 = graphics mode
    ; bit 7 = linear framebuffer available
    ; --------------------------------------------------------

    mov ax, [es:bp + vbe_mode_info - vbe_mode]

    test ax, 0001h
    jz _next_mode

    test ax, 0010h
    jz _next_mode

    test ax, 0080h
    jz _next_mode


    ; The kernel writes u32 pixels. Accept 8:8:8 RGB with either an
    ; unused high byte (24-bit color) or an 8-bit fourth channel.
    call _get_pixel_mode
    cmp al, 0ffh
    je _next_mode

    ; --------------------------------------------------------
    ; X resolution
    ; offset 12h
    ; --------------------------------------------------------

    mov ax, [es:bp + vbe_mode_info - vbe_mode + 12h]
    mov [ds:bp + vbe_current_x - vbe_mode], ax


    ; --------------------------------------------------------
    ; Y resolution
    ; offset 14h
    ; --------------------------------------------------------

    mov ax, [es:bp + vbe_mode_info - vbe_mode + 14h]
    mov [ds:bp + vbe_current_y - vbe_mode], ax


    ; --------------------------------------------------------
    ; Calculate X * Y
    ; --------------------------------------------------------

    mov ax, [ds:bp + vbe_current_x - vbe_mode]
    mov cx, [ds:bp + vbe_current_y - vbe_mode]

    mul cx

    ; DX:AX = current area

    cmp dx, [ds:bp + vbe_best_area - vbe_mode + 2]
    ja _new_best

    jb _next_mode

    cmp ax, [ds:bp + vbe_best_area - vbe_mode]
    ja _new_best
    jmp _next_mode


_new_best:

    mov [ds:bp + vbe_best_mode - vbe_mode], bx

    mov ax, [ds:bp + vbe_current_x - vbe_mode]
    mov [ds:bp + vbe_best_x - vbe_mode], ax

    mov ax, [ds:bp + vbe_current_y - vbe_mode]
    mov [ds:bp + vbe_best_y - vbe_mode], ax

    mov ax, [ds:bp + vbe_current_x - vbe_mode]
    mov cx, [ds:bp + vbe_current_y - vbe_mode]
    mul cx

    mov [ds:bp + vbe_best_area - vbe_mode], ax
    mov [ds:bp + vbe_best_area - vbe_mode + 2], dx

    jmp _next_mode


    ; ========================================================
    ; Found highest resolution
    ; ========================================================

_found_mode:

    cmp word [ds:bp + vbe_best_mode - vbe_mode], 0
    je _failure


    ; ========================================================
    ; Get information about selected mode one more time
    ; ========================================================

    mov ax, ds
    mov es, ax

    mov di, vbe_mode_info - vbe_mode
    add di, bp

    mov cx, [ds:bp + vbe_best_mode - vbe_mode]

    mov ax, 4F01h
    int 10h

    cmp ax, 004Fh
    jne _failure

    call _get_pixel_mode
    cmp al, 0ffh
    je _failure
    movzx eax, al
    mov [ds:bp + vbe_mode.pixel_mode - vbe_mode], eax


    ; ========================================================
    ; Set mode
    ; ========================================================

    mov bx, [ds:bp + vbe_best_mode - vbe_mode]
    or bx, 4000h

    mov ax, 4F02h
    int 10h

    cmp ax, 004Fh
    jne _failure


    ; ========================================================
    ; Save horizontal resolution
    ; ========================================================

    xor eax, eax
    mov ax, [ds:bp + vbe_best_x - vbe_mode]
    mov [ds:bp + vbe_mode.horizontal_resolution - vbe_mode], eax


    ; ========================================================
    ; Save vertical resolution
    ; ========================================================

    xor eax, eax
    mov ax, [ds:bp + vbe_best_y - vbe_mode]
    mov [ds:bp + vbe_mode.vertical_resolution - vbe_mode], eax


    ; ========================================================
    ; Save pixels per scanline
    ;
    ; VBE gives BytesPerScanLine at +10h. VBE 3.0 has a separate
    ; LinBytesPerScanLine at +32h for linear framebuffer modes.
    ;
    ; pixels_per_scanline = BytesPerScanLine / 4 (32-bit pixels)
    ; ========================================================

    xor eax, eax
    mov ax, [es:bp + vbe_mode_info - vbe_mode + 10h]
    cmp word [es:bp + vbe_controller - vbe_mode + 4], 0300h
    jb .have_scanline_bytes
    cmp word [es:bp + vbe_mode_info - vbe_mode + 32h], 0
    je .have_scanline_bytes
    mov ax, [es:bp + vbe_mode_info - vbe_mode + 32h]
.have_scanline_bytes:
    shr eax, 2

    mov [ds:bp + vbe_mode.pixels_per_scanline - vbe_mode], eax


    ; ========================================================
    ; Save framebuffer address
    ;
    ; PhysBasePtr = offset 28h
    ; ========================================================

    mov eax, [es:bp + vbe_mode_info - vbe_mode + 28h]
    mov [ds:bp + vbe_mode.framebuffer_address - vbe_mode], eax


    ; ========================================================
    ; Copy vbe_mode structure to caller
    ; ========================================================

    mov edx, [ds:bp + target_address - vbe_mode]

    mov si, bp

    mov ecx, 6

.copy_loop:
    mov ebx, edx
    mov di, bx
    and di, 000fh
    shr ebx, 4

    mov es, bx

    mov eax, [ds:si]
    mov [es:di], eax

    add si, 4
    add edx, 4

    loop .copy_loop


    xor eax, eax
    jmp _done


_failure:

    mov eax, 1


_done:

    pop fs
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx

    retf


; Return the kernel's pixel format in AL: 0 = RGB (red at bit 16),
; 1 = BGR (red at bit 0), or FFh for an unsupported VBE layout.
; VBE 3.0 has separate mask fields for the linear framebuffer.
_get_pixel_mode:
    push si
    cmp byte [es:bp + vbe_mode_info - vbe_mode + 19h], 32
    jne .unsupported
    cmp byte [es:bp + vbe_mode_info - vbe_mode + 1bh], 6
    jne .unsupported

    mov si, 1fh               ; VBE 2.0 direct-color mask fields
    cmp word [es:bp + vbe_controller - vbe_mode + 4], 0300h
    jb .check_masks
    cmp byte [es:bp + vbe_mode_info - vbe_mode + 36h], 0
    je .check_masks
    mov si, 36h               ; VBE 3.0 linear-framebuffer mask fields

.check_masks:
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode], 8
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 2], 8
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 3], 8
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 4], 8
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 6], 0
    je .check_order
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 6], 8
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 7], 24
    jne .unsupported

.check_order:
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 1], 16
    jne .maybe_bgr
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 5], 0
    jne .unsupported
    xor al, al
    jmp .done

.maybe_bgr:
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 1], 0
    jne .unsupported
    cmp byte [es:bp + si + vbe_mode_info - vbe_mode + 5], 16
    jne .unsupported
    mov al, 1
    jmp .done

.unsupported:
    mov al, 0ffh
.done:
    pop si
    ret
    
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
e820_next: dd 0
e820_count: dd 0
