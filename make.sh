cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

nasm -f elf64 kernel/entry.asm -o entry.o
nasm -f elf64 kernel/x86-64/ports.asm -o ports.o
nasm -f elf64 kernel/x86-64/lowlevel.asm -o lowlevel.o
gcc -O2 -g -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/kernel.c -o kernel.o
gcc -O2 -g -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/vga.c -o vga.o
gcc -O2 -g -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/rtc.c -o rtc.o
gcc -O2 -g -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory.c -o memory.o
ld -T kernel/link.ld -o kernel.elf *.o

rm *.o

cd ..

