#ifndef ELF_INTERNAL_H
#define ELF_INTERNAL_H


#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN


#include "uint_definitions.h"

#define ET_NONE 0x00
#define ET_REL 0x01
#define ET_EXEC 0x02


#define X86 0x03
#define AMD_X86_64 0x3E

#define PT_LOAD 0x00000001

typedef struct {
    u8 ELF_MAGIC[4];
    u8 EI_CLASS; // 1 = 32-bit format, 2 = 64-bit format
    u8 EI_DATA;
    u8 EI_VERSION;
    u8 EI_OSABI; // identifies the target operating system abi
    u8 ABI_VERSION;
    u32 Padding0;
    u16 Padding1;
    u8 Padding2;
    u16 e_type; 
    u16 e_machine;
    u32 e_version; // typically set to 1
    u32 e_entry; // the address of the entry point
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf_header;



typedef struct {
    u8 ELF_MAGIC[4];
    u8 EI_CLASS; // 1 = 32-bit format, 2 = 64-bit format
    u8 EI_DATA;
    u8 EI_VERSION;
    u8 EI_OSABI; // identifies the target operating system abi
    u8 ABI_VERSION;
    u32 Padding0;
    u16 Padding1;
    u8 Padding2;
    u16 e_type; 
    u16 e_machine;
    u32 e_version; // typically set to 1
    u64 e_entry; // the address of the entry point
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf_header_64;



typedef struct {
    u32 p_type;
    u32 p_offset; // offset of the segment in the file image
    u32 p_vaddr; // virtual address of segment
    u32 p_paddr; // physical address of segment
    u32 p_filesz; // size in bytes of segment in file image
    u32 p_memsz; // size of segment in memory
    u32 p_flags; // on 32-bit p_flags is before p_align
    u32 p_align; // 0 and 1 specify no alignment, otherwise should be a power of two
} __attribute__((packed)) program_header;



typedef struct {
    u32 p_type;
    u32 p_flags; // on 64-bit, p_flags is after p_type
    u64 p_offset; // offset of the segment in the file image
    u64 p_vaddr; // virtual address of segment
    u64 p_paddr; // physical address of segment
    u64 p_filesz; // size in bytes of segment in file image
    u64 p_memsz; // size of segment in memory
    u64 p_align; // 0 and 1 specify no alignment, otherwise should be a power of two
} __attribute__((packed)) program_header_64;


KERNEL_EXTERN_C_END

#endif