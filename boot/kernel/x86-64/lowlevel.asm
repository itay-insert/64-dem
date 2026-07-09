BITS 64

global stack_get
global check_1gb_PageSupport
global enable_paging
global kernel_trampoline
global cpu_vendor
global cpu_brand
global cpu_speed

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


cpu_vendor:
push rbx
xor eax, eax
cpuid
mov [rdi], ebx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov byte [rdi], 0
pop rbx
ret



cpu_brand:
push rbx
mov eax, 0x80000002
cpuid
mov [rdi], eax
add rdi, 4
mov [rdi], ebx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov eax, 0x80000003
cpuid
mov [rdi], eax
add rdi, 4
mov [rdi], ebx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov eax, 0x80000004
cpuid
mov [rdi], eax
add rdi, 4
mov [rdi], ebx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov byte [rdi], 0
pop rbx
ret

cpu_speed:
push rbx
xor eax, eax
cpuid
cmp eax, 0x16
jb leaf_not_supported
mov eax, 0x16
cpuid
mov [rdi], eax
add rdi, 4
mov [rdi], ebx
add rdi, 4
mov [rdi], ecx
xor eax, eax ; 0 for success
pop rbx
ret
leaf_not_supported:
mov eax, 1 ; 1 for error
pop rbx
ret