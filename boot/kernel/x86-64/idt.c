#include "uint_definitions.h"

#define trap 0xF
#define interrupt 0xE

typedef struct {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed)) idt_entry;


idt_entry idt[256];

void idt_set_gate(u8 entry, u64 address, u8 ist, u8 type) {
    idt[entry] = (idt_entry){
        .offset_low = (u16)(address & 0xffff),
        .selector = 0x08,
        .ist = ist,
        .type_attr = (u8)(0x80 | type),
        .offset_mid = (u16)((address >> 16) & 0xffff),
        .offset_high = (u32)(address >> 32),
        .reserved = 0
    };
}

void setup_idt(void) {

    
}