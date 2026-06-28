cd EFI
cd boot

./make.sh

cd ..
cd ..

cd boot

gcc -fpic -pie -ffreestanding -nostdlib -c loader/loader.c -o loader.o
ld -T loader/link.ld -o loader.elf *.o

rm *.o

cd ..

