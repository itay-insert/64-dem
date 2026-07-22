cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

nasm -f elf64 kernel/entry.asm -o entry.o
nasm -f elf64 kernel/x86-64/ports.asm -o ports.o
nasm -f elf64 kernel/x86-64/lowlevel.asm -o lowlevel.o
nasm -f elf64 kernel/x86-64/cpuid.asm -o cpuid.o
nasm -f elf64 kernel/x86-64/interrupts.asm -o interrupts.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/kernel.c -o kernel.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/font.c -o font.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/vga.c -o vga.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/rtc.c -o rtc.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/pci.c -o pci.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory.c -o memory.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/paging.c -o paging.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/cpudir.c -o cpudir.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/tss.c -o tss.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/gdt.c -o gdt.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/acpi.c -o acpi.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/idt.c -o idt.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/handlers.c -o handlers.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/apic.c -o apic.o
ld -T kernel/link.ld -o kernel.elf *.o

rm *.o

cd ..

