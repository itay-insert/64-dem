cd ..

nasm -f elf64 hello/entry.asm -o entry.o

gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c hello/hello.c -o hello.o

ld -T lib/link.ld -o hello.elf *.o
rm *.o