ENTRY(_start)

PHDRS {
    image PT_LOAD FLAGS(7);
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS {
    .text.start 0x40000028 : {
        KEEP(*(.text.start))
    } :image

    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) *(.eh_frame .eh_frame.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.*) *(COMMON) } :image

    ASSERT(_start == 0x40000028, "64-dem requires Vim's entry at 0x40000028")
}
