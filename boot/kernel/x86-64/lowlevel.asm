BITS 64


extern APIC_base
extern IO_APIC

global stack_get
global check_1gb_PageSupport
global enable_paging
global kernel_trampoline
global load_gdt
global discover_APIC
global load_idt
global disable_pic
global read_msr
global write_msr
global lapic_read
global lapic_write
global ioapic_read
global ioapic_write
global enable_interrupts
global disable_interrupts


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


load_gdt:
lgdt [rdi]

push qword 0x08
lea rax, [rel reload]
push rax
retfq

reload:
mov ax, 0x10
mov ds, ax
mov es, ax
mov ss, ax

mov ax, 0x28
ltr ax
ret



discover_APIC:
mov ecx, 0x1B
rdmsr
shl rdx, 32
or rax, rdx
and rax, 0xfffffffffffff000
mov rdx, 0xFFFF900000000000
add rax, rdx
ret

load_idt:
lidt [rdi]
ret


disable_pic:
cli ; disable interrupts in case

mov dx, 0x20 ; remap master

mov al, 0x11 
out dx, al

inc dx

mov al, 0x20
out dx, al

mov al, 0x4
out dx, al

mov al, 0x1
out dx, al


mov dx, 0xA0 ; remap slave

mov al, 0x11
out dx, al

inc dx

mov al, 0x28
out dx, al

mov al, 0x2
out dx, al

mov al, 0x1
out dx, al

mov al, 0xFF ; mask pic
out 0x21, al
out 0xA1, al

ret 


read_msr:
mov ecx, edi
rdmsr
shl rdx, 32
or rax, rdx
ret

write_msr:
mov eax, esi
shr rsi, 32
mov edx, esi
mov ecx, edi
wrmsr
ret


lapic_read:
mov rax, [rel APIC_base]
add rax, rdi
mov eax, [rax]
ret

lapic_write:
mov rax, [rel APIC_base]
add rax, rdi
mov dword [rax], esi
ret

ioapic_read:
mov rax, [rel IO_APIC]
add rax, rdi
mov eax, [rax]
ret

ioapic_write:
mov rax, [rel IO_APIC]
add rax, rdi
mov dword [rax], esi
ret

enable_interrupts:
sti
ret

disable_interrupts:
cli
ret