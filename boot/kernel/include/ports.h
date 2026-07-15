#include <stdint.h>
#include "uint_definitions.h"

#ifndef PORTS_H
#define PORTS_H

u8 inb(u16 port);
void outb(u16 port, u8 byte);
u16 inw(u16 port);
void outw(u16 port, u16 word);
void io_wait(void);
u32 inl(u16 port);
void outl(u16 port, u32 value);


#endif