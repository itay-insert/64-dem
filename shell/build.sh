#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

names=("hello" "mem" "cmd")

for name in "${names[@]}"; do 
    nasm -f elf64 lib/sys.asm -o sys.o
    gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c lib/stdio.c -o stdio.o
    cd "$name"
    chmod +x local.sh
    ./local.sh
    cd ..
done
