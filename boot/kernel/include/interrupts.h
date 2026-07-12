#ifndef INTERRUPTS_H
#define INTERRUPTS_H

extern void *exception_stub_table[32];
void isr_eoi(void);

#endif
