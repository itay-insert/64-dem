[BITS 16]
org 0x7c00

BOOTSEG equ 0

_start:
    cli
    mov ax, BOOTSEG ; initiallizing segment registers, except cs
    mov ds, ax ; because initializing cs with a mov instruction is
    mov es, ax ; invalid
    mov ss, ax

    mov sp, 0x7c00 ; setting up a stack

    mov [boot_drive], dl
    

    call _search_partition

    cmp ax, 0
    je error

    cmp ah, 0x0B
    je _skip_second_check
    cmp ah, 0x0C
    je _skip_second_check
    jmp error
    _skip_second_check:

    call _open_root
    

    _search_for_stage2:
    xor bx, bx
    mov di, 0x9000
    mov cx, 32
    call _bios_disk_services
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

    halt:
    jmp halt

    alter:
        mov ax, [es:di+0x14]
        shl eax, 16
        mov ax, [es:di+0x1A]
        call _convert_cluster_to_lba
        cmp byte [enable], 2
        jb _search_for_stage2
        xor bx, bx
        mov di, 0x1000
        mov cx, 1
        call _bios_disk_services
        mov dl, [boot_drive] ; hope bios didn't destroy it
        mov si, boot_drive
        jmp 0x1000:0x0000

    error:
        mov ah, 0x0E
        mov bh, 0
        mov al, 'E'
        int 0x10
        jmp halt

_search_partition:
    mov di, 0x7c00
    add di, 446
    mov al, [di]
    add di, 4
    mov ah, [di]
    ret

_get_fat32_start:
    mov di, 0x7c00
    add di, 446
    mov eax, [di + 8]
    mov [partition_start], eax
    ret

_bios_disk_services:
    mov dl, [boot_drive]
    mov si, dap
    mov byte [si], 0x10  ; size of disk i/o packet
    mov [si + 8], eax ; lba_address
    mov word [si + 4], bx ; buffer offset
    mov word [si + 6], di ; buffer segment
    mov word [si + 2], cx; sectors to read
    mov ah, 0x42
    int 0x13 ; bios call to read sectors
    jc error
    ret


_convert_cluster_to_lba:
    sub eax, 2
    movzx ecx, byte [sectors_per_cluster] 
    imul eax, ecx
    add eax, [first_data_lba]
    ret

_open_root:
    call _get_fat32_start
    xor bx, bx
    mov di, 0x9000
    mov cx, 1
    call _bios_disk_services
    xor di, di
    mov ax, 0x9000
    mov es, ax
    mov al, [es:di+0x0D]
    mov [sectors_per_cluster], al
    mov eax, [es:di+0x2C]
    mov [root_cluster], eax
    movzx edx, word [es:di+0x0E]
    mov al, [es:di+0x10]
    mov [number_of_fats], al
    mov eax, [es:di+0x24]
    movzx ecx, byte [number_of_fats]
    imul eax, ecx
    add eax, [partition_start]
    add eax, edx
    mov [first_data_lba], eax
    mov eax, [root_cluster]
    call _convert_cluster_to_lba
    ret


folder db 'BOOT       '
file db 'STAGE2  BIN'

boot_drive db 0
partition_start dd 0
sectors_per_cluster db 0
number_of_fats db 0
root_cluster dd 0
first_data_lba dd 0


pointer dw folder
counter db 0

max_count db 11

enable db 0

dap:
times 16 db 0

times 440-($-$$) db 0