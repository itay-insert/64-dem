#include "uint_definitions.h"
#include "lowlevel.h"
#include "interrupts.h"

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

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idtr_t;

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

void setup_execptions(void) {
    idt_set_gate(0, (u64)isr_no_error, 0, interrupt);
    idt_set_gate(6, (u64)isr_no_error, 0, interrupt);
    idt_set_gate(8, (u64)isr_error, 0, interrupt);
    idt_set_gate(13, (u64)isr_error, 0, interrupt);
    idt_set_gate(14, (u64)page_fault_handler, 0, interrupt);

    idtr_t idtr = {0, 0};
    idtr.limit = sizeof(idt_entry) * 256 - 1;
    idtr.base  = (uint64_t)&idt[0];

    load_idt((u64)&idtr);

}