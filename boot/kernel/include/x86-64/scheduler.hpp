#pragma once
#include "x86-64/paging.h"
#include "filesystem/fs.hpp"


typedef struct file_id file_id;

struct file_id {
    u64 file_handle;
    u64 file_offset;
    u32 pid;
    int flags;
};





typedef struct {
    u32 PID;
    PAGING_LOOKUP_DESCRIPTOR *MappedPages;
    u64 PageCount;
    void *buffer;
    u64 file_handle;
    u64 rsp;
    file_id *files;
    int file_count;
} Proccess;




void StartProccess(u64 file_handle, BootFileSystem *fat32);
