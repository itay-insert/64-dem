BITS 64

global stack_get
global check_1gb_PageSupport
global enable_paging
global kernel_trampoline

section .text

stack_get:
mov rax, rsp
add rax, 8
ret



check_1gb_PageSupport:
push rbx

mov eax, 0x80000001
cpuid
test edx,1<<26
setnz al
movzx eax, al

pop rbx
ret



enable_paging:
mov rax, rdi
mov cr3, rax
ret


kernel_trampoline:
mov rax, r8
jmp rax


