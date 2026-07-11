#include <stdint.h>
#include "uint_definitions.h"
#include "lowlevel.h"
#include "tss.h"


typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry;

typedef struct {
    uint16_t limit_low;       
    uint16_t base_low;        
    uint8_t  base_mid;       
    uint8_t  access;         
    uint8_t  limit_high_flags;
    uint8_t  base_high;      
    uint32_t base_upper;
    uint32_t reserved;       
} __attribute__((packed)) tss_descriptor;

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));


u8 gdt[56];


void setup_gdt(void) {
    gdt_entry *gdt_ptr = (gdt_entry *)gdt;

    // null descriptor 
    gdt_ptr[0] = (gdt_entry){0};

    // kernel code 0x08
    gdt_ptr[1] = (gdt_entry){
        .limit_low = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = 0x9A,
        .granularity = 0xA0,
        .base_high = 0
    };

    // kernel data 0x10
    gdt_ptr[2] = (gdt_entry){
        .limit_low = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = 0x92,
        .granularity = 0x00,
        .base_high = 0
    };

    // user code
    gdt_ptr[3] = (gdt_entry){
        .limit_low = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = 0xFA,
        .granularity = 0xA0,
        .base_high = 0
    };

    // user data
    gdt_ptr[4] = (gdt_entry){
        .limit_low = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = 0xF2,
        .granularity = 0x00,
        .base_high = 0
    };

    tss_descriptor *gdt_tss = (tss_descriptor *)(gdt+0x28);

    u64 tss_addr = (u64)&tss;

    gdt_tss[0] = (tss_descriptor){
        .limit_low = sizeof(struct tss64) - 1,
        .base_low = (u16)(tss_addr & 0xffff),
        .base_mid = (u8)((tss_addr >> 16) & 0xff),
        .access = 0x89,
        .limit_high_flags = (u8)(((sizeof(struct tss64) - 1) >> 16) & 0x0f),
        .base_high = (u8)((tss_addr >> 24) & 0xff),
        .base_upper = (u32)(tss_addr >> 32),
        .reserved = 0
    };
    
    struct gdtr gdtr = {
        .limit = sizeof(gdt)-1,
        .base = (u64)gdt
    };

    load_gdt((u64)&gdtr);

}

