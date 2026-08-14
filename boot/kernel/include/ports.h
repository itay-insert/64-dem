#ifndef PORTS_H
#define PORTS_H

#include "uint_definitions.h"

static inline u8 inb(u16 port)
  {
      u8 value;

      __asm__ volatile (
          "inb %1, %0"
          : "=a"(value)
          : "Nd"(port)
          : "memory"
      );

      return value;
  }

static inline u16 inw(u16 port)
  {
      u16 value;

      __asm__ volatile (
          "inw %1, %0"
          : "=a"(value)
          : "Nd"(port)
          : "memory"
      );

      return value;
  }

static inline u32 inl(u16 port)
  {
      u32 value;

      __asm__ volatile (
          "inl %1, %0"
          : "=a"(value)
          : "Nd"(port)
          : "memory"
      );

      return value;
  }

static inline void outb(u16 port, u8 value)
  {
      __asm__ volatile (
          "outb %0, %1"
          :
          : "a"(value), "Nd"(port)
          : "memory"
      );
  }

static inline void outw(u16 port, u16 value)
  {
      __asm__ volatile (
          "outw %0, %1"
          :
          : "a"(value), "Nd"(port)
          : "memory"
      );
  }

static inline void outl(u16 port, u32 value)
  {
      __asm__ volatile (
          "outl %0, %1"
          :
          : "a"(value), "Nd"(port)
          : "memory"
      );
  }

static inline void io_wait(void)
  {
      __asm__ volatile (
          "outb %%al, $0x80"
          :
          : "a"((u8)0)
          : "memory"
      );
  }




#endif