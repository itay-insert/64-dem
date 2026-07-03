cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

nasm -f elf64 kernel/x86-64/ports.asm -o ports.o
gcc -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/kernel.c -o kernel.o
gcc -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/vga.c -o vga.o
gcc -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/rtc.c -o rtc.o
ld -T kernel/link.ld -o kernel.elf *.o

rm *.o

cd ..

