#include <stddef.h>
#include "x86-64/memory/kmalloc.h"

void *operator new(size_t size) {
    return kmalloc(size);
}

void *operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void *ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    kfree(ptr);
}