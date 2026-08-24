ENTRY(_start)

SECTIONS {
    . = 0xFFFF800000000000;

    .text ALIGN (4K) : {
        _start = .;
        *(.text.start)
        *(.text*)
    }

    .rodata ALIGN (4K) : {
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

    .data ALIGN (4K) : {
        *(.data*)
    }

    .bss ALIGN (4K) : {
        *(COMMON*)
        *(.bss*)
    }
}
