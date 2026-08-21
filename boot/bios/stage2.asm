[BITS 16]
org 0x10000

_start:
    mov bh, 0
    mov ah, 0x0e
    mov al, 'S'
    int 0x10

    halt:
        jmp halt