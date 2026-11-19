cd ..


gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c calc/calc.c -o calc.o

ld -T lib/link.ld -o calc.elf *.o
rm *.o