BITS 64

global cpu_vendor
global cpu_brand
global cpu_cores
global cpu_speed
global cpu_model
global vm_type

section .text

cpu_vendor:
push rbx
mov eax, 1
cpuid
bt ecx, 31
setc al
cmp al, 1
je leaf_not_supported
xor eax, eax
cpuid
mov [rdi], ebx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov byte [rdi], 0
xor eax, eax
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


cpu_cores:
push rbx
xor eax, eax
cpuid
cmp eax, 0xb
jb leaf_not_supported
mov eax, 0xb
mov ecx, 0
cpuid
mov [rdi], ebx
add rdi, 4
cmp ebx, 0
je leaf_bug
mov eax, 0xb
mov ecx, 1
cpuid
mov [rdi], ebx
cmp ebx, 0
je leaf_bug
xor eax, eax ; 0 for success
pop rbx
ret
leaf_bug:
mov eax, 2
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


cpu_model:
push rbx
mov eax, 1
cpuid
mov [rdi], ecx
add rdi, 4
mov [rdi], edx
pop rbx
ret

vm_type:
push rbx
mov eax, 0x40000000
cpuid
mov [rdi], ebx
add rdi, 4
mov [rdi], ecx
add rdi, 4
mov [rdi], edx
add rdi, 4
mov byte [rdi], 0
pop rbx
ret

