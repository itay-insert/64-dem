#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "filesystem/fs.hpp"
#include "filesystem/elf_internal.h"
#include "x86-64/memory/memory.h"


static inline u64 calc_size(u64 file_handle, BootFileSystem *fat32, u32 offset, u32 size, u16 e_phnum) {
    u8 *buffer = new u8[size];

    fat32->read_file(file_handle, (u64)offset, size, buffer);
    program_header *segment = (program_header *)buffer;

    u64 Size = 0;
    for (u16 i = 0; i < e_phnum; i++) {
        if (segment->p_type == PT_LOAD) {
            Size += segment->p_memsz;
        }

        segment++;
    }

    delete[] buffer;
    return Size;
}



static inline u64 calc_size_64(u64 file_handle, BootFileSystem *fat32, u64 offset, u32 size, u16 e_phnum) {
    u8 *buffer = new u8[size];

    fat32->read_file(file_handle, offset, size, buffer);
    program_header_64 *segment = (program_header_64 *)buffer;

    u64 Size = 0;
    for (u16 i = 0; i < e_phnum; i++) {
        if (segment->p_type == PT_LOAD) {
            Size += segment->p_memsz;
        }

        segment++;
    }

    delete[] buffer;
    return Size;
}



u64 elf_calculate_size(u64 file_handle, BootFileSystem *fat32) {
    u8 *buffer = new u8[512];

    fat32->read_file(file_handle, 0, 512, buffer);

    elf_header_64 *header = (elf_header_64 *)buffer;
    if (header->EI_CLASS == 1) {
        elf_header *header32 = (elf_header *)buffer;
        u32 size = ((u32)header32->e_phentsize) * ((u32)header32->e_phnum);
        u32 offset = header32->e_phoff;
        u16 e_phnum = header32->e_phnum;
        delete[] buffer;
        return calc_size(file_handle, fat32, offset, size, e_phnum);
    } else if (header->EI_CLASS == 2) {
        u32 size = ((u32)header->e_phentsize) * ((u32)header->e_phnum);
        u64 offset = header->e_phoff;
        u16 e_phnum = header->e_phnum;
        delete[] buffer;
        return calc_size_64(file_handle, fat32, offset, size, e_phnum);
    }
}



void load_elf_64(u64 file_handle, BootFileSystem *fat32, void *buff) {
    u8 *buffer = new u8[512];

    fat32->read_file(file_handle, 0, 512, buffer);

    elf_header_64 *header = (elf_header_64 *)buffer;
    u32 size = ((u32)header->e_phentsize) * ((u32)header->e_phnum);
    u64 offset = header->e_phoff;    
    u16 e_phnum = header->e_phnum;
    delete[] buffer;

    buffer = new u8[size];
    fat32->read_file(file_handle, offset, size, buffer);

    program_header_64 *segment = (program_header_64 *)buffer;

    for (u16 i = 0; i < e_phnum; i++) {
        if (segment->p_type == PT_LOAD) {
            u64 offset = segment->p_vaddr - 0x40000000;
            memset(buff+offset, 0, segment->p_memsz);
            fat32->read_file(file_handle, segment->p_offset, (u32)segment->p_filesz, (void *)buff+offset);
        }

        segment++;
    }

}

