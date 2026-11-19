cd ..


gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c clock/clock.c -o clock.o

ld -T lib/link.ld -o clock.elf *.o
rm *.o