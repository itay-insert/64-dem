#ifndef C_COMPAT_H
#define C_COMPAT_H

/* Keep shared kernel headers usable from both C and C++. */
#ifdef __cplusplus
#define KERNEL_EXTERN_C_BEGIN extern "C" {
#define KERNEL_EXTERN_C_END }
#define KERNEL_STATIC_ASSERT(condition, message) static_assert(condition, message)
#define KERNEL_ALIGNOF(type) alignof(type)
#else
#define KERNEL_EXTERN_C_BEGIN
#define KERNEL_EXTERN_C_END
#define KERNEL_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#define KERNEL_ALIGNOF(type) _Alignof(type)
#endif

#endif
