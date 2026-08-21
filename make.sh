cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

nasm -f elf64 kernel/entry.asm -o entry.o
nasm -f elf64 kernel/x86-64/lowlevel.asm -o lowlevel.o
nasm -f elf64 kernel/x86-64/cpuid.asm -o cpuid.o
nasm -f elf64 kernel/x86-64/interrupts.asm -o interrupts.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/kernel.c -o kernel.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/display/font.c -o font.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/display/vga.c -o vga.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/rtc.c -o rtc.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/pci/pci.c -o pci.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/pci/pci_names.c -o pci_names.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/storage/storage.c -o storage.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/storage/ide/atahlp.c -o atahlp.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/storage/ide/ataid.c -o ataid.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/storage/ide/ata.c -o ata.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/memhlp.c -o memhlp.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/alloc.c -o alloc.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/maphlp.c -o maphlp.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/kalloc.c -o kalloc.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/dmadef.c -o dmadef.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/dmahlp.c -o dmahlp.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/memory/dma.c -o dma.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/paging.c -o paging.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/cpudir.c -o cpudir.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/tss.c -o tss.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/gdt.c -o gdt.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/acpi.c -o acpi.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/idt.c -o idt.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/handlers.c -o handlers.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/x86-64/apic.c -o apic.o
gcc -O2 -march=x86-64 -Ikernel/include -fpie -ffreestanding -nostdlib -mno-red-zone -c kernel/drivers/xhci.c -o xhci.o
ld -T kernel/link.ld -o kernel.elf *.o

rm *.o

nasm -f bin bios/stage2.asm -o stage2.bin

cd ..
