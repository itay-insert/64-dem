#ifndef SYS_H
#define SYS_H

#include "c_compat.h"
KERNEL_EXTERN_C_BEGIN

#include "uint_definitions.h"
u64 get_syscall(void);
void enter_userspace(u64 user_rsp);

KERNEL_EXTERN_C_END
#endif