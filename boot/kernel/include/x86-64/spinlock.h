#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "uint_definitions.h"

typedef struct {
    volatile u32 locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELAXED);
}

static inline void spin_lock(spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
          __asm__ volatile ("pause");
        }
    }
}

static inline void spin_unlock(spinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

#endif
