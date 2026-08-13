#!/bin/sh
set -eu

test_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(dirname -- "$test_script_dir")
cd "$project_root"

cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined \
    -Iboot/kernel/include tests/test_dma_allocator.c \
    -o /tmp/64-dem-test-dma
/tmp/64-dem-test-dma

cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined \
    -Iboot/kernel/include tests/test_ata_identify.c \
    -o /tmp/64-dem-test-ata
/tmp/64-dem-test-ata
