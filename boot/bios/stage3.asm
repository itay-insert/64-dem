[BITS 16]
org 0


_start:
    xor ax, ax
    mov ss, ax
    mov ax, 0x1000
    mov ds, ax
    mov [boot_drive], dl

    mov bh, 0
    mov ah, 0x0E
    mov al, '3'
    int 0x10

    call _open_root
    push eax
    call _find_fat_start
    pop eax
    _search_for_stage4:
    xor bx, bx
    mov di, 0x9000
    mov cx, 32
    call 0x1000:_bios_disk_services
    mov ax, 0x9000
    mov es, ax
    xor di, di

    here:
    mov byte [counter], 0
    mov si, [pointer]
    mov cx, 11
    mov ah, [es:di]
    cmp ah, 0
    je dir
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
        call 0x1000:_convert_cluster_to_lba
        cmp byte [enable], 2
        jb _search_for_stage4
        movzx eax, word [es:di+0x14]
        shl eax, 16
        mov ax, [es:di+0x1A]
        mov [current_cluster], eax
        mov [starting_cluster], eax

        mov dx, 4
        _cluster_load_loop:
        push dx
        call 0x1000:_convert_cluster_to_lba
        mov bx, [addr]
        mov di, [addr+2]
        shl di, 12
        movzx ecx, byte [sectors_per_cluster]
        call 0x1000:_bios_disk_services
        
        movzx ecx, byte [sectors_per_cluster]
        shl ecx, 9
        add dword [addr], ecx
        
        call 0x1000:_find_next_cluster
        pop dx

        and eax, 0x0FFFFFFF

        cmp eax, 0x0FFFFFF8
        jae end_of_chain

        cmp eax, 0x0FFFFFF7
        je error
        
        cmp eax, 2
        jb error

        mov [current_cluster], eax
        
        dec dx
        cmp dx, 0
        jne _cluster_load_loop

        end_of_chain:
        mov bh, 0
        mov ah, 0x0E
        mov al, '4'
        int 0x10

        mov eax, 0x80000
        call _convert_addr
        mov es, cx
        mov di, ax

        mov eax, [es:di+0x1c]
        mov [e_phoff], eax

        mov ax, [es:di+0x2a]
        mov [e_phentsize], ax

        mov ax, [es:di+0x2c]
        mov [e_phnum], ax

        mov eax, [starting_cluster]
        call 0x1000:_convert_cluster_to_lba
        mov [starting_lba], eax
    
        call _load_segments

        mov ax, 0x0003
        int 0x10

        in al, 0x92
        test al, 2
        jnz after
        or al, 2
        and al, 0xFE
        out 0x92, al
        after:

        mov edx, 0x10000
        add edx, _bios_services

        lgdt [gdt_descriptor]
        mov eax, cr0
        or al, 1
        mov cr0, eax

        jmp dword 0x08:0x18000

    gdt_start:
        dd 0x0
        dd 0x0


        dw 0xffff ; kernel code segment (0x08)
        dw 0x0000 
        db 0x00
        db 10011010b
        db 11001111b
        db 0x00

        dw 0xffff ; kernel data segment (0x10)
        dw 0x0000
        db 0x00
        db 10010010b
        db 11001111b
        db 0x00
    gdt_end:
    gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0x10000 + gdt_start

    _println:
        push ecx
        mov bh, 0
        mov ah, 0x0E
        mov al, [si]
        int 0x10
        inc si
        pop ecx
        loop _println
        retf

    error:
        mov bh, 0
        mov ah, 0x0E
        mov al, 'E'
        int 0x10
        jmp halt
    
    dir:
        mov bh, 0
        mov ah, 0x0E
        mov al, 'D'
        int 0x10
        jmp halt

    _get_fat32_start:
        xor ax, ax
        mov es, ax
        mov di, 0x7c00
        add di, 446
        mov eax, [es:di+8]
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
        retf

    _convert_cluster_to_lba:
        sub eax, 2
        movzx ecx, byte [sectors_per_cluster] 
        imul eax, ecx
        add eax, [first_data_lba]
        retf
    
    _convert_lba_to_cluster:
        sub eax, [first_data_lba]
        xor edx, edx
        movzx ecx, byte [sectors_per_cluster]
        div ecx
        add eax, 2
        retf

    _open_root:
        call _get_fat32_start
        xor bx, bx
        mov di, 0x9000
        mov cx, 1
        call 0x1000:_bios_disk_services
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
        call 0x1000:_convert_cluster_to_lba
        ret

     _find_fat_start:
        mov eax, [partition_start]
        xor bx, bx
        mov di, 0x9000
        mov cx, 1
        call 0x1000:_bios_disk_services
        mov ax, 0x9000
        mov es, ax
        xor di, di
        movzx eax, word [es:di+0x0E]
        add eax, [partition_start]
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
        call 0x1000:_bios_disk_services

        mov di, [current_cluster]
        shl di, 2
        and di, 511

        mov ax, 0x9000
        mov es, ax
        mov eax, [es:di]
        cmp eax, 0x0FFFFFFF
        je skip_save
        and eax, 0x0FFFFFFF
        mov [current_cluster], eax
        skip_save:
        retf
    

    _convert_addr:
        mov ecx, eax
        shr ecx, 16
        shl ecx, 12
        movzx eax, ax
        ret


    _zero_initialize:
        xor eax, eax
        rep stosb
        ret

    _convert_sector_count:
        xor edx, edx
        movzx ecx, byte [sectors_per_cluster]
        div ecx
        cmp eax, 0
        jne skip_set
        mov eax, 1
        skip_set:
        retf

    _load_clusters:

        push ecx
        mov [current_cluster], eax
        call 0x1000:_convert_cluster_to_lba
        add eax, [sector_remainder]
        push eax
        mov eax, [ld_addr]
        call _convert_addr
        mov bx, ax
        mov di, cx
        pop eax
        movzx ecx, byte [sectors_per_cluster]
        call 0x1000:_bios_disk_services
        call 0x1000:_find_next_cluster
        mov eax, [current_cluster]
        mov ecx, 512
        movzx edx, byte [sectors_per_cluster]
        imul ecx, edx
        add dword [ld_addr], ecx
        pop ecx
        loop _load_clusters
        retf

    ld_addr dd 0
    ctrf dd 0
    _load_segment:
        push ecx ; save important registers for caller
        push di
        push es
        mov eax, [p_vaddr]
        mov [ld_addr], eax
        call _convert_addr ; convert the 32-bit virtual address to a 16-bit offset and segment
        mov di, ax
        mov es, cx
        push di
        mov ecx, [p_memsz]
        add ecx, 0xFFF ; align p_memsz to 4k
        and ecx, ~0xFFF
        call _zero_initialize ; zero_initiallize the memory region
        pop di
        mov ecx, [p_filesz]
        add ecx, 0xFFF ; align p_filesz to 4k
        shr ecx, 12
        load_loop:
        push ecx
        mov eax, [p_offset]
        shr eax, 9 ; divide by 512 to extract how many sectors to add to the starting sector
        add eax, dword [starting_lba]
        call 0x1000:_convert_lba_to_cluster
        mov [sector_remainder], edx
        mov [ctrf], eax
        mov eax, 8
        call 0x1000:_convert_sector_count
        mov ecx, eax
        mov eax, [ctrf]
        call 0x1000:_load_clusters
        pop ecx
        add dword [p_offset], 4096
        loop load_loop
        pop es
        pop di
        pop ecx
        ret
        

    _load_segments:
        mov ecx, [e_phoff]
        add di, cx
        mov ecx, [e_phnum]
        _seg_loop:
        mov eax, [es:di]
        cmp eax, 1
        jne skip_load
        mov eax, [es:di+0x04]  ; p_offset
        mov ebx, [es:di+0x08]  ; p_vaddr
        mov [p_offset], eax
        mov [p_vaddr], ebx
        mov eax, [es:di+0x10]  ; p_filesz
        mov ebx, [es:di+0x14]  ; p_memsz
        mov [p_filesz], eax
        mov [p_memsz], ebx
        cmp eax, 0
        je skip_load
        call _load_segment
        skip_load:
        add di, [e_phentsize]
        loop _seg_loop
        ret
        


folder db 'BOOT       '
file db 'STAGE4  ELF'

_bios_services:
dw _bios_disk_services
dw _convert_cluster_to_lba
dw _convert_lba_to_cluster
dw _convert_sector_count
dw _find_next_cluster
dw _println
code_seg dw 0x1000
boot_drive db 0
partition_start dd 0
fat_start dd 0
sectors_per_cluster db 0
number_of_fats db 0
root_cluster dd 0
first_data_lba dd 0

current_cluster dd 0
starting_cluster dd 0
starting_lba dd 0

sector_remainder dd 0
pointer dw folder
counter db 0

max_count db 11

enable db 0

addr dd 0x80000
tar_addr dd 0x18000

dap:
times 16 db 0

e_phoff dd 0
e_phentsize dw 0
e_phnum dw 0

p_offset dd 0 ; offset 0x04
p_vaddr dd 0 ; offset 0x08 
p_filesz dd 0 ; offset 0x10
p_memsz dd 0 ; offset 0x14

times 32768-($-$$) db 0