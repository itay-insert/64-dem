ENTRY(_start)

SECTIONS {
    . = 0x40000028;

    .text ALIGN (1) : SUBALIGN(1) {
        _start = .;
        *(.text.start)
        *(.text*)
    }

    .rodata ALIGN (1) : SUBALIGN(1) {
        *(.rodata*)
    }

    .init_array ALIGN (8) : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    }

    .fini_array ALIGN (8) : {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    }

    .data ALIGN (1) : SUBALIGN(1) {
        *(.data*)
    }

    .bss ALIGN (1) : SUBALIGN(1) {
        *(COMMON*)
        *(.bss*)
    }
}
