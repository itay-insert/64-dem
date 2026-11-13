nasm -f elf64 sys.asm -o sys.o
nasm -f elf64 entry.asm -o entry.o

gcc -O2 -march=x86-64 -Iinclude -fpie -ffreestanding -nostdlib -mno-red-zone -c hello.c -o hello.o

ld -T link.ld -o hello.elf *.o

rm *.o