[BITS 16]
org 0x8000

_start:
    mov bh, 0
    mov ah, 0x0e
    mov al, '2'
    int 0x10


    mov eax, [si+13]

    call word [si+2]
    
    

    halt:
        jmp halt