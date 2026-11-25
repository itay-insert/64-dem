cd ..

gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c memcons/memcons.c -o memcons.o

ld -T lib/link.ld -o memcons.elf *.o
rm *.o