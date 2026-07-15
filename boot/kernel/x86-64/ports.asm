BITS 64
global inb
global outb
global inw
global outw
global io_wait
global inl
global outl

section .text

inb:
mov dx, di
in al, dx
movzx eax, al
ret

outb:
mov dx, di
mov eax, esi
out dx, al
ret

inw:
mov dx, di
in ax, dx
movzx eax, ax
ret

outw:
mov dx, di
mov eax, esi
out dx, ax
ret

io_wait:
mov al, 0
mov dx, 0x80
out dx, al
ret

inl:
mov dx, di
in eax, dx
ret

outl:
mov dx, di
mov eax, esi
out dx, eax
ret