#!/bin/sh
set -eu

test_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(dirname -- "$test_script_dir")
cd "$project_root"

kernel_includes="-Iboot/kernel/include"

c++ -std=c++11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined \
    $kernel_includes tests/test_usb_keyboard_selection.cpp \
    -o /tmp/64-dem-test-usb-keyboard-selection
/tmp/64-dem-test-usb-keyboard-selection

c++ -std=c++11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined \
    $kernel_includes tests/test_usb_bulk_endpoints.cpp \
    -o /tmp/64-dem-test-usb-bulk-endpoints
/tmp/64-dem-test-usb-bulk-endpoints

c++ -std=c++11 -Wall -Wextra -Werror -O1 -g -fno-rtti \
    -fno-builtin -ffunction-sections -fdata-sections -Wl,--gc-sections \
    $kernel_includes tests/test_usb_irq.cpp boot/kernel/drivers/usb/usb_irq.cpp \
    -o /tmp/64-dem-test-usb-irq
/tmp/64-dem-test-usb-irq

cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined \
    $kernel_includes tests/test_pci_irq.c boot/kernel/drivers/pci/irq.c \
    -o /tmp/64-dem-test-pci-irq
/tmp/64-dem-test-pci-irq

c++ -std=c++11 -Wall -Wextra -Werror -O1 -g -fno-rtti \
    -fsanitize=undefined $kernel_includes tests/test_hid_keyboard.cpp \
    boot/kernel/drivers/keyboard/hid_report.cpp \
    boot/kernel/drivers/keyboard/keyboard.cpp -o /tmp/64-dem-test-hid
/tmp/64-dem-test-hid


c++ -std=c++11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined $kernel_includes tests/test_usb_periodic.cpp \
    -o /tmp/64-dem-test-usb-periodic
/tmp/64-dem-test-usb-periodic

c++ -std=c++11 -Wall -Wextra -Werror -O1 -g -fno-rtti \
    -fsanitize=undefined $kernel_includes tests/test_usb_async.cpp \
    boot/kernel/drivers/usb/usb_async.cpp -o /tmp/64-dem-test-usb-async
/tmp/64-dem-test-usb-async

cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=undefined -ffreestanding \
    -Dmemset=kernel_memset -Dmemcpy=kernel_memcpy -Dmemcmp=kernel_memcmp \
    -DDMA_BASE=0x100000000ULL -DDMA_POOL=0x200000000ULL \
    $kernel_includes tests/test_dma_allocator.c \
    boot/kernel/x86-64/memory/memhlp.c \
    boot/kernel/x86-64/memory/alloc.c \
    boot/kernel/x86-64/memory/maphlp.c \
    boot/kernel/x86-64/memory/valloc.c \
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

cc -std=c11 -Wall -Wextra -Werror -Wno-error -O1 -g \
    -fsanitize=undefined -DVA_TREE=0x300000000ULL \
    $kernel_includes tests/test_va_alloc.c \
    boot/kernel/x86-64/memory/va_alloc.c \
    boot/kernel/x86-64/memory/va_def.c \
    -o /tmp/64-dem-test-va-alloc
/tmp/64-dem-test-va-alloc
