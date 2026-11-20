BITS 64
global _start
global _exit
global exit
extern main
extern dem_vim_init

section .text.start progbits alloc exec nowrite align=1
_start:
    ; enter_userspace(user_rsp, arg) leaves the filename address in RSI.
    mov r12, rsi
    call dem_vim_init
    mov edi, 8
    lea rsi, [rel argv]
    test r12, r12
    jz .run
    cmp byte [r12], 0
    je .run
    lea rax, [rel end_options]
    mov [rsi + 8 * 8], rax
    mov [rsi + 9 * 8], r12
    mov edi, 10
.run:
    call main
    jmp _exit

section .text
exit:
_exit:
    mov eax, 4
    syscall
    ud2

section .rodata
name: db "vim", 0
term_flag: db "-T", 0
term_name: db "ansi", 0
vimrc_flag: db "-u", 0
vimrc_none: db "NONE", 0
no_swap: db "-n", 0
command_flag: db "--cmd", 0
startup_options:
    db "set nobackup nowritebackup backspace=indent,eol,start ttimeout ttimeoutlen=50"
    db " esckeys virtualedit=onemore | nnoremap <special> <BS> X"
    db " | nnoremap <special> <C-H> X", 0
end_options: db "--", 0

section .data
align 8
argv: dq name, term_flag, term_name, vimrc_flag, vimrc_none, no_swap, command_flag, startup_options
    dq 0, 0, 0
