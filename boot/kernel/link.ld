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
    .data ALIGN (4K) : {
        *(.data*)
    }
    .bss ALIGN (4K) : {
        *(COMMON*)
        *(.bss*)
    }
}
