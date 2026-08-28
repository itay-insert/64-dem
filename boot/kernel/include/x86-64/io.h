#ifndef IO_H
#define IO_H

#include "x86-64/ports.h"
#include "x86-64/acpi.h"

static inline u8 io_inb(u64 io_addr, int mode) {
    if (mode == MMIO) {
        volatile u8 *ptr = (volatile u8 *)io_addr;
        return *ptr;
    } else {
        return inb((u16)io_addr);
    }
}    

static inline u16 io_inw(u64 io_addr, int mode) {
    if (mode == MMIO) {
        volatile u16 *ptr = (volatile u16 *)io_addr;
        return *ptr;
    } else {
        return inw((u16)io_addr);
    }
}

static inline u32 io_inl(u64 io_addr, int mode) {
    if (mode == MMIO) {
        volatile u32 *ptr = (volatile u32 *)io_addr;
        return *ptr;
    } else {
        return inl((u16)io_addr);
    }
}


static inline u64 io_inq(u64 io_addr, int mode) {
    if (mode == MMIO) {
        volatile u64 *ptr = (volatile u64 *)io_addr;
        return *ptr;
    } 


    return 0;
}


static inline void io_outb(u64 io_addr, u8 val, int mode) {
    if (mode == MMIO) {
        volatile u8 *ptr = (volatile u8 *)io_addr;
        *ptr = val;
    } else {
        outb((u16)io_addr, val);
    }
}

static inline void io_outw(u64 io_addr, u16 val, int mode) {
    if (mode == MMIO) {
        volatile u16 *ptr = (volatile u16 *)io_addr;
        *ptr = val;
    } else {
        outw((u16)io_addr, val);
    }
}

static inline void io_outl(u64 io_addr, u32 val, int mode) {
    if (mode == MMIO) {
        volatile u32 *ptr = (volatile u32 *)io_addr;
        *ptr = val;
    } else {
        outl((u16)io_addr, val);
    }
}

static inline void io_outq(u64 io_addr, u64 val, int mode) {
    if (mode == MMIO) {
        volatile u64 *ptr = (volatile u64 *)io_addr;
        *ptr = val;
    }

}
#endif