#ifndef INTERRUPTS_H
#define INTERRUPTS_H

void isr_eoi(void);
void isr_no_error(void);
void isr_error(void);
void page_fault_handler(void);

#endif