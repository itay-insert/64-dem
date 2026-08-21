[BITS 16]
org 0x7c00

BOOTSEG equ 0

_start:
    cli
    mov ax, BOOTSEG ; initiallizing segment registers
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov cs, ax

    mov sp, 0x7c00 ; setting up a stack

    mov si, message

_printlen:
    push bx
    _main:
    lodsb
    test al, al
    jz .done

    cmp al, 0x0A
    jz _move_down_line

    mov ah, 0x0E ; bios output
    mov bh, 0
    int 0x10

    _return
    mov dx, 0xe9 ; qemu output
    out dx, al
    
    jmp _main

    _move_down_line:
        mov al, 0x0D
        mov ah, 0x0E
        mov bh, 0
        int 0x10

        mov al, 0x0A
        int 0x10

        jmp _return

    .done:
        pop bx
        ret


message db 'loading stage two...', 10, 0





times 440-($-$$) db 0