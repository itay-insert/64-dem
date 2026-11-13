#pragma once
#include "x86-64/paging.h"
#include "filesystem/fs.hpp"

typedef struct {
    u32 PID;
    PAGING_LOOKUP_DESCRIPTOR *MappedPages;
    u64 PageCount;
    void *buffer;
    u64 file_handle;
    u64 rsp;
} Proccess;

void StartProccess(u64 file_handle, BootFileSystem *fat32);
