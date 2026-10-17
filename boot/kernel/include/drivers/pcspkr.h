#ifndef PCSPKR_H
#define PCSPKR_H

#include "c_compat.h"

KERNEL_EXTERN_C_BEGIN

void speaker_on(unsigned hz);
void speaker_off();

KERNEL_EXTERN_C_END

#endif