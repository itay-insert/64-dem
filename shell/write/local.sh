cd ..

gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c write/write.c -o write.o

ld -T lib/link.ld -o write.elf *.o
rm *.o