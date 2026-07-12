#ifndef TSS_H
#define TSS_H

#include <stdint.h>

struct tss64 {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint32_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint32_t reserved2;
    uint32_t reserved3;

    uint16_t reserved4;

    uint16_t iomap_base;
} __attribute__((packed));


extern struct tss64 tss;
void tss_init(void);


#endif
