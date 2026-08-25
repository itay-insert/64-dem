[BITS 16]
org 0x8000

_start:
    mov [boot_drive], dl

    mov bh, 0
    mov ah, 0x0e
    mov al, '2'
    int 0x10

    mov bp, si

    call _find_fat_start

    mov eax, [bp+13]

    call word [bp+2]
    
    _search_for_stage3:
    xor bx, bx
    mov di, 0x9000
    mov cx, 32
    call word [bp]
    mov ax, 0x9000
    mov es, ax
    xor di, di

    here:
    mov byte [counter], 0
    mov si, [pointer]
    mov cx, 11
    mov ah, [es:di]
    cmp ah, 0
    je error
    push si
    push di
    _loop:
    mov al, [si]
    mov ah, [es:di]
    cmp al, ah
    sete al
    add byte [counter], al
    inc si
    inc di
    loop _loop
    pop di
    pop si

    cmp byte [counter], 11
    sete al

    movzx eax, al
    mul byte [max_count]

    add word [pointer], ax
    cmp word [pointer], si
    setne al
    
    mov ah, [enable]
    add byte [enable], al


    cmp ah, [enable]
    jb alter
    add di, 32
    jmp here


    alter:
        mov ax, [es:di+0x14]
        shl eax, 16
        mov ax, [es:di+0x1A]
        call word [bp+2]
        cmp byte [enable], 2
        jb _search_for_stage3
        movzx eax, word [es:di+0x14]
        shl eax, 16
        mov ax, [es:di+0x1A]
        mov [current_cluster], eax

        _cluster_load_loop:

        call word [bp+2]
        mov bx, [addr]
        mov di, [addr+2]
        shl di, 12
        movzx ecx, byte [bp+11]
        call word [bp]
        
        movzx ecx, byte [bp+11]
        shl ecx, 9
        add dword [addr], ecx
        
        call _find_next_cluster
        and eax, 0x0FFFFFFF

        cmp eax, 0x0FFFFFF8
        jae end_of_chain

        cmp eax, 0x0FFFFFF7
        je error
        
        cmp eax, 2
        jb error

        mov [current_cluster], eax
        jmp _cluster_load_loop

        end_of_chain:
        mov dl, [boot_drive]
        jmp 0x1000:0x0000




    error:
        mov ah, 0x0E
        mov bh, 0
        mov al, 'E'
        int 0x10
    halt:
        jmp halt

    _find_fat_start:
        mov eax, [bp+7]
        xor bx, bx
        mov di, 0x9000
        mov cx, 1
        call word [bp]
        mov ax, 0x9000
        mov es, ax
        xor di, di
        movzx eax, word [es:di+0x0E]
        add eax, [ds:bp+7]
        mov [fat_start], eax
        ret

    _find_next_cluster:
        mov eax, [current_cluster]
        shl eax, 2

        mov edx, eax
        shr eax, 9
        and edx, 511
        add eax, [fat_start]

        xor bx, bx
        mov di, 0x9000
        mov cx, 1
        call word [bp]

        mov di, [current_cluster]
        shl di, 2
        and di, 511

        mov ax, 0x9000
        mov es, ax
        mov eax, [es:di]
        and eax, 0x0FFFFFFF
        mov [current_cluster], eax
        
        ret





folder db 'BOOT       '
file db 'STAGE3  BIN'


pointer dw folder

fat_start dd 0
boot_drive db 0

counter db 0
enable db 0

max_count db 11

current_cluster dd 0

addr dd 0x10000

times 512-($-$$) db 0