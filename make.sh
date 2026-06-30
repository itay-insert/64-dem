cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

gcc -fpic -pie -ffreestanding -nostdlib -c kernel/kernel.c -o kernel.o
ld -T kernel/link.ld -o kernel.elf *.o

rm *.o

cd ..

