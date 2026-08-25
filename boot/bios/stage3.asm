[BITS 16]
org 0


_start:
    xor ax, ax
    mov ss, ax
    mov ax, 1
    mov ds, ax
    mov [boot_drive], dl

    mov bh, 0
    mov ah, 0x0E
    mov al, '3'
    int 0x10


    halt:
        jmp halt



boot_drive db 0