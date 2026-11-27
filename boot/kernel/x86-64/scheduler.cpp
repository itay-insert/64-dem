#include "x86-64/scheduler.hpp"
#include "filesystem/elf.hpp"
#include "x86-64/memory/memory.h"
#include "x86-64/memory/malloc.h"
#include "filesystem/elf.hpp"
#include "x86-64/sys.h"
#include "x86-64/efi_memory_types.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/display/vga.h"

extern int GbPageSupport;
extern BootFileSystem *fat32_fs;
extern "C" void clock();

Proccess *pid = nullptr;
int active_proccesses = 0;
int current_proccess = 0;
int render_rights = 0;

int last_pid = 0;

extern "C" void print_proccess_mem(void) {
    if (pid == nullptr || active_proccesses == 0) {
        printf("No active processes.\n");
        return;
    }

    for (int i = 0; i < active_proccesses; ++i) {
        const Proccess *proc = &pid[i];
        u64 bytes = proc->allocated_bytes + sizeof(Proccess)
            + proc->PageCount * sizeof(PAGING_LOOKUP_DESCRIPTOR)
            + (u64)proc->file_count * sizeof(file_id);
        u64 unit = 1024;
        const char *label = "KB";
        if (bytes >= (1ULL << 30)) {
            unit = 1ULL << 30;
            label = "GB";
        } else if (bytes >= (1ULL << 20)) {
            unit = 1ULL << 20;
            label = "MB";
        }
        u64 fraction = (bytes % unit) * 100 / unit;
        printf("PID %u: %lu.%s%lu %s\n", proc->PID, bytes / unit,
               fraction < 10 ? "0" : "", fraction, label);
    }
}

extern u64 rflags;
extern u64 rip;
u64 former_user_rsp = 0;

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
    u64 pages = (size + 4095) >> 12;
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


void StartProccess(u64 file_handle, BootFileSystem *fat32, u64 arg) {
    constexpr u64 user_base = 0x40000000;
    char argument[4096];
    u64 argument_size = 0;
    if (arg) {
        if (!pid || arg < user_base || arg - user_base >= pid[current_proccess].allocated_bytes)
            return;
        u64 available = pid[current_proccess].allocated_bytes - (arg - user_base);
        const char *source = (const char *)arg;
        while (argument_size < sizeof(argument) && argument_size < available) {
            char ch = source[argument_size];
            argument[argument_size++] = ch;
            if (ch == '\0')
                break;
        }
        if (argument[argument_size - 1] != '\0')
            return;
    }

    Proccess *new_pid = increase_proccesses();
    current_proccess = active_proccesses - 1;
    render_rights = current_proccess;

    if (active_proccesses > 1) {
        pid[current_proccess-1].halt = true;
    }

    u64 size = elf_calculate_size(file_handle, fat32);

    size = (size + 0x41000);

    // _start calls main, so its initial stack must be 16-byte aligned.
    new_pid->rsp = (user_base + size - argument_size) & ~0xFULL;
    new_pid->file_count = 0;
    new_pid->files = nullptr;

    new_pid->file_handle = file_handle;
    new_pid->buffer = malloc(size, true);
    new_pid->halt = false;

    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((u8 *)new_pid->buffer - sizeof(EFI_MEMORY_DESCRIPTOR));
    new_pid->allocated_bytes = desc->NumberOfPages * 4096ULL;
    u64 physical_address = desc->PhysicalStart;
    construct_mapping(new_pid, size, physical_address);

    memset(new_pid->buffer, 0, size);

    load_elf_64(file_handle, fat32, new_pid->buffer);

    new_pid->cursor_coulmn = 0;
    new_pid->cursor_row = 0;
    cursor_Setpos(0, 0);
    // The child replaces the caller's mappings. Put its argument above its initial stack.
    u64 child_argument = argument_size ? new_pid->rsp : 0;
    if (argument_size)
        memcpy((void *)child_argument, argument, argument_size);
    enter_userspace(new_pid->rsp, child_argument);
    
}


void resume_proccess(int index) {
    current_proccess = index;
    render_rights = index;
    cursor_Setpos(pid[index].cursor_coulmn, pid[index].cursor_row);
    u64 base = 0x40000000;
    for (u64 i = 0; i < pid[index].PageCount; i++) {
        int type = pid[index].MappedPages[i].Page_Type;
        u64 size = 0x1000;


        if (type == MB)
            size = 0x200000;
        else if (type == GB)
            size = 0x40000000;

        create_mapping(base, pid[index].MappedPages[i].physical_address, size, 0x07, KernelPML4);
        base += size;
    }

    resume_userspace();
}




extern "C" void terminate_procces(int index) {
    if (pid == nullptr || index < 0 || index >= active_proccesses)
        return;

    // Process images are mapped allocations, not kmalloc allocations.
    free(pid[index].buffer, true);
    delete[] pid[index].MappedPages;
    for (int fd = 0; fd < pid[index].file_count; ++fd) {
        const file_id &file = pid[index].files[fd];
        if (file.active)
            fat32_fs->close_file(file.file_handle);
    }
    if (pid[index].files)
        kfree(pid[index].files);

    for (int i = index; i + 1 < active_proccesses; ++i)
        pid[i] = pid[i + 1];
    --active_proccesses;

    if (active_proccesses == 0) {
        delete[] pid;
        pid = nullptr;
        current_proccess = 0;
        return;
    }

    pid[active_proccesses] = {0};
    // Keep the allocation: shrinking is unnecessary and can fail.
    if (index > 0 && pid[index - 1].halt) {
        pid[index - 1].halt = false;
        resume_proccess(index - 1);
    }
    current_proccess = index < active_proccesses ? index : active_proccesses - 1;
}


extern "C" void SwitchProccesses(void) {
    if (active_proccesses < 2)
        return; // not enough active proccesses for multitasking

    int index = current_proccess;
    index = (index < (active_proccesses-1)) ? (index + 1) : 0;
    resume_proccess(index);
}


extern "C" void sys_exit() {
    terminate_procces(current_proccess);
    clock();
}


