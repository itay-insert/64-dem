#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

gcc -O2 -march=x86-64 -Ilib/include -fpie -ffreestanding -nostdlib -mno-red-zone -c cmd/cmd.c -o cmd.o

ld -T lib/link.ld -o cmd.elf *.o
rm -- *.o
