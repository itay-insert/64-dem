#include "x86-64/scheduler.hpp"
#include "filesystem/elf.hpp"
#include "x86-64/memory/memory.h"
#include "x86-64/memory/malloc.h"
#include "filesystem/elf.hpp"
#include "x86-64/sys.h"
#include "x86-64/efi_memory_types.h"
#include "x86-64/memory/kmalloc.h"

extern int GbPageSupport;

Proccess *pid = nullptr;
int active_proccesses = 0;

int last_pid = 0;

Proccess *increase_proccesses() {
    if (pid == nullptr || active_proccesses == 0) {
        pid = new Proccess[1];
        active_proccesses = 1;
        pid[0] = {0};
        pid[0].PID = last_pid;
        last_pid++;
        return &pid[0];
    }

    u64 old_size = active_proccesses * sizeof(Proccess);
    u64 new_size = old_size + sizeof(Proccess);
    pid = (Proccess *)krealloc(pid, old_size, new_size);
    active_proccesses++;
    return &pid[active_proccesses-1];
}


PAGING_LOOKUP_DESCRIPTOR *Pages = nullptr;

u64 current_user_rsp = 0;

static u64 count_mapping(u64 size, u64 physical_address) {
    u64 count = 0;
    u64 virtual_address = 0x40000000; // user start
    u64 pages = (size + 511) >> 12;
    Pages = new PAGING_LOOKUP_DESCRIPTOR[pages];
    u64 virtual_end = virtual_address + (pages<<12);
    while (virtual_address < virtual_end) {
        size = virtual_end - virtual_address;
        int mb_enable = (virtual_address & 0x1fffff) | (physical_address & 0x1fffff);
        int gb_enable = (virtual_address & 0x3fffffff) | (physical_address & 0x3fffffff);
        u64 gb_count = size / 0x40000000;
        u64 mb_count = size / 0x200000;
        u64 kb_count = pages;
        if (gb_count > 0 && gb_enable == 0 && GbPageSupport == 1) {
            Pages[count].physical_address = physical_address;
            Pages[count].Page_Type = GB;
            Pages[count].attributes = 0x07;
            Pages[count].status = 0;
            virtual_address += 0x40000000;
            PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(virtual_address, KernelPML4);
            physical_address = lookup.physical_address;
            pages -= 262144;
            count++;
        } else if (mb_count > 0 && mb_enable == 0) {
            Pages[count].physical_address = physical_address;
            Pages[count].Page_Type = MB;
            Pages[count].attributes = 0x07;
            Pages[count].status = 0;
            virtual_address += 0x200000;
            PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(virtual_address, KernelPML4);
            physical_address = lookup.physical_address;
            pages -= 512;
            count++;
        } else if (pages > 0) {
            Pages[count].physical_address = physical_address;
            Pages[count].Page_Type = KB;
            Pages[count].attributes = 0x07;
            Pages[count].status = 0;
            virtual_address += 0x1000;
            PAGING_LOOKUP_DESCRIPTOR lookup = paging_lookup(virtual_address, KernelPML4);
            physical_address = lookup.physical_address;
            pages--;
            count++;
        }

    }

    return count;
}



void construct_mapping(Proccess *proc, u64 size, u64 physical_address) {
    u64 count = count_mapping(size, physical_address);

    proc->PageCount = count;
    proc->MappedPages = new PAGING_LOOKUP_DESCRIPTOR[count];
    for (u64 i = 0; i < count; i++) 
        proc->MappedPages[i] = Pages[i];
    
    delete[] Pages;

}


void StartProccess(u64 file_handle, BootFileSystem *fat32) {

    Proccess *new_pid = increase_proccesses();
    u64 size = elf_calculate_size(file_handle, fat32);

    size = (size + 0x41000);

    new_pid->rsp = 0x40000000 + size;

    new_pid->file_handle = file_handle;
    new_pid->buffer = malloc(size, true);

    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((u8 *)new_pid->buffer - sizeof(EFI_MEMORY_DESCRIPTOR));
    u64 physical_address = desc->PhysicalStart;
    construct_mapping(new_pid, size, physical_address);

    memset(new_pid->buffer, 0, size);

    load_elf_64(file_handle, fat32, new_pid->buffer);

    enter_userspace(new_pid->rsp);
    
}

