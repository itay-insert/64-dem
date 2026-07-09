#include "uint_definitions.h"

#ifndef LOWLEVEL_H
#define LOWLEVEL_H



u64 stack_get(void);
int check_1gb_PageSupport(void);
void enable_paging(u64 PML4_address);

#endif
