BITS 64
global inb
global outb
global inw
global outw

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
