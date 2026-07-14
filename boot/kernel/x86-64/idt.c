#include "uint_definitions.h"
#include "lowlevel.h"
#include "interrupts.h"

#define INTERRUPT_GATE 0xE

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

static idt_entry idt[256];

static void idt_set_gate(u8 entry, u64 address, u8 ist, u8 type) {
    idt[entry] = (idt_entry){
        .offset_low = (u16)(address & 0xffff),
        .selector = 0x08,
        .ist = (u8)(ist & 7),
        .type_attr = (u8)(0x80 | type),
        .offset_mid = (u16)((address >> 16) & 0xffff),
        .offset_high = (u32)(address >> 32),
        .reserved = 0
    };
}

void setup_execptions(void) {
    u64 stub_table_base = (u64)exception_stub_table;

    for (u8 vector = 0; vector < 32; vector++) {
        u64 stub = stub_table_base + exception_stub_table[vector];
        idt_set_gate(vector, stub, 0, INTERRUPT_GATE);
    }

    /* Double fault switches to TSS.IST1 even if the current stack is broken. */
    u64 double_fault_stub = stub_table_base + exception_stub_table[8];
    idt_set_gate(8, double_fault_stub, 1, INTERRUPT_GATE);

    u64 spurious = (u64)&spurious_stub + spurious_stub;

    idt_set_gate(0xFF, spurious, 0, INTERRUPT_GATE);
    
    u64 timer = (u64)&timer_stub + timer_stub;

    idt_set_gate(0x40, timer, 0, INTERRUPT_GATE);

    idtr_t idtr = {
        .limit = sizeof(idt) - 1,
        .base = (u64)idt
    };
    load_idt((u64)&idtr);
}
