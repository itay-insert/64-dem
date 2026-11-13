#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

#include "x86-64/paging.h"

typedef struct {
    u32 PID;
    PAGING_LOOKUP_DESCRIPTOR *MappedPages;
    u64 PageCount;
} Proccess;


KERNEL_EXTERN_C_END