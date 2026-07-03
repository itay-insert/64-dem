#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

#define i8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t 

#ifndef PORTS_H
#define PORTS_H

u8 inb(u16 port);
void outb(u16 port, u8 byte);
u16 inw(u16 port);
void outw(u16 port, u16 word);


#endif