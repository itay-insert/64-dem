#ifndef CPUDIR_H
#define CPUDIR_H

#include "uint_definitions.h"
#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

extern char buffer_vendor[30];
extern char buffer_brand[50];
extern u32 buffer_speed[4];
extern u32 buffer_model[3];
extern u32 buffer_cores[3];
void cpu_info(void);

KERNEL_EXTERN_C_END

#endif
