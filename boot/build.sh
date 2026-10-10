nasm -f elf32 bios/stage4/entry.asm -o entry.o
gcc -O2 -Ibios/stage4/include -m32 -fno-pie -ffreestanding -nostdlib -mno-red-zone -c bios/stage4/stage4.c -o stage4.o
gcc -O2 -Ibios/stage4/include -m32 -fno-pie -ffreestanding -nostdlib -mno-red-zone -c bios/stage4/vga.c -o vga.o
gcc -O2 -Ibios/stage4/include -m32 -fno-pie -ffreestanding -nostdlib -mno-red-zone -c bios/stage4/font.c -o font.o
ld -m elf_i386 -T bios/stage4/link.ld *.o -o stage4.elf

rm *.o