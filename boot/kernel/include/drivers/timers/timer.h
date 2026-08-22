#ifndef TIMER_H
#define TIMER_H

typedef struct {
    union {
        long ret;
        int ret;
        u64 ret;
        u32 ret;
        u16 ret;
        u8 ret;
    } ret_descriptor;
} timer_ret_t;

extern timer_ret_t timer_call_wrapper(u64 function_addr, u64 function_wrapper, 
int arg1, int arg2, ...);

#endif