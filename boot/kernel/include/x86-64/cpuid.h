#include "uint_definitions.h"


#ifndef CPUID_H
#define CPUID_H

int cpu_vendor(u64 string_base);
void cpu_brand(u64 string_base);
int cpu_cores(u64 addr_base);
int cpu_speed(u64 addr_base);
u32 cpu_model(u64 addr_base);
void vm_type(u64 addr_base);


#endif