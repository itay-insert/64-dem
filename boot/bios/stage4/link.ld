ENTRY(_start)

SECTIONS {
    . = 0x18000;

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

    /* The BIOS loader skips NOLOAD segments. Keep both maps away from its
       64 KiB zeroing window and from the 0x90000 disk transfer buffer. */
    .memory_maps 0x30000 (NOLOAD) : {
        *(.memory_maps*)
    }
    ASSERT(SIZEOF(.memory_maps) <= 0x10000, "stage 4 memory maps exceed 64 KiB")
}
