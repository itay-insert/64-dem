#!/bin/sh
set -eu

test_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(dirname -- "$test_script_dir")
cd "$project_root"

kernel_includes="-Iboot/kernel/include"

cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined -ffreestanding \
    -Dmemset=kernel_memset -Dmemcpy=kernel_memcpy -Dmemcmp=kernel_memcmp \
    -DDMA_BASE=0x100000000ULL -DDMA_POOL=0x200000000ULL \
    $kernel_includes tests/test_dma_allocator.c \
    boot/kernel/x86-64/memory/memhlp.c \
    boot/kernel/x86-64/memory/alloc.c \
    boot/kernel/x86-64/memory/maphlp.c \
    boot/kernel/x86-64/memory/kalloc.c \
    boot/kernel/x86-64/memory/dmadef.c \
    boot/kernel/x86-64/memory/dmahlp.c \
    boot/kernel/x86-64/memory/dma.c \
    -o /tmp/64-dem-test-dma
/tmp/64-dem-test-dma

cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined -Wno-unused-parameter \
    $kernel_includes tests/test_ata_identify.c \
    -o /tmp/64-dem-test-ata
/tmp/64-dem-test-ata
