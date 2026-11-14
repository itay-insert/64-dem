cd ..

nasm -f elf64 mem/entry.asm -o entry.o

gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c mem/mem.c -o mem.o

ld -T lib/link.ld -o mem.elf *.o
rm *.o