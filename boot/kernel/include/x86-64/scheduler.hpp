#pragma once
#include "x86-64/paging.h"
#include "filesystem/fs.hpp"
#include "x86-64/process_mem.h"


typedef struct file_id file_id;

struct file_id {
    u64 file_handle;
    u64 file_offset;
    int flags;
    bool active;
};





typedef struct {
    u32 PID;
    PAGING_LOOKUP_DESCRIPTOR *MappedPages;
    u64 PageCount;
    void *buffer;
    u64 allocated_bytes;
    u64 file_handle;
    u64 rsp;
    file_id *files;
    int file_count;
    bool halt;
    int cursor_row;
    int cursor_coulmn;
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rbp;
    u64 rsi;
    u64 rdi;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
} Proccess;




void StartProccess(u64 file_handle, BootFileSystem *fat32, u64 arg);
