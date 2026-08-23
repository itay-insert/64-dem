#include <stdbool.h>
#include "uint_definitions.h"
#include "x86-64/ports.h"
#include "x86-64/paging.h"
#include "x86-64/acpi.h"


#define PM_TIMER_FREQUENCY 3579545ULL
#define PM_TIMER_MASK      0x00ffffffu

#define BYTE 0
#define WORD 1
#define DWORD 2

u64 PM_base = 0;

typedef struct {
    void (*write)(u64, u8, u32);
    u32 (*read)(u64, u8);
} __attribute__((packed)) pmctl;


void write_mmio(u64 offset, u8 type, u32 value) {
    switch (type) {

        case BYTE:
            u8 val = (u8)value;
            volatile u8 *ptr = (volatile u8 *)(PM_base + offset);
            break;

        case WORD:
            u16 val = (u16)value;
            volatile u16 *ptr = (volatile u16 *)(PM_base + offset);
            break;

        default:
            u32 val = value;
            volatile u32 *ptr = (volatile u32 *)(PM_base + offset);
            break;

    }

    *ptr = val;     
}


void write_io(u64 offset, u8 type, u32 value) {
    u16 port = ((u16)PM_base + (u16)offset);

    switch (type) {
        case BYTE:
            outb(port, (u8)value);
            break;

        case WORD:
            outw(port, (u16)value);
            break;

        default:
            outl(port, value);
            break;

    }

}


u32 read_mmio(u64 offset, u8 type) {
    switch (type) {

        case BYTE:
            volatile u8 *ptr = (volatile u8 *)(PM_base+offset);
            break;

        case WORD:
            volatile u16 *ptr = (volatile u16 *)(PM_base+offset);
            break;

        default:
            volatile u32 *ptr = (volatile u32 *)(PM_base+offset);
            break;
    }

    return ((u32)*ptr);
}


u32 read_io(u64 offset, u8 type) {
    u16 port = ((u16)PM_base + (u16)offset);

    switch (type) {

        case BYTE:
            u8 val = inb(port);
            break;

        case WORD:
            u16 val = inw(port);
            break;

        default:
            u32 val = inl(port);
            break;

    }

    return ((u32)val);
}


int pm_init(PM_ret desc) {

} 