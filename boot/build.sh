gcc -O2 -m32 -fno-pie -ffreestanding -nostdlib -mno-red-zone -c bios/stage4/stage4.c -o stage4.o
ld -m elf_i386 -T bios/stage4/link.ld *.o -o stage4.elf

rm *.o