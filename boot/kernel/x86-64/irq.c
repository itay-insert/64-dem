#include "x86-64/irq.h"
#include "x86-64/paging.h"

static struct {
    device_irq_handler handler;
    void *context;
} handlers[DEVICE_IRQ_COUNT];

static u64 lock_irqs(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void unlock_irqs(u64 flags) {
    if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
}
u8 device_irq_allocate(device_irq_handler handler, void *context) {
    if (!handler) return 0;
    u64 flags = lock_irqs();
    for (u32 i = 0; i < DEVICE_IRQ_COUNT; ++i) {
        if (!handlers[i].handler) {
            handlers[i].context = context;
            __atomic_store_n(&handlers[i].handler, handler, __ATOMIC_RELEASE);
            unlock_irqs(flags);
            return DEVICE_IRQ_FIRST + i;
        }
    }
    unlock_irqs(flags);
    return 0;
}
void device_irq_release(u8 vector) {
    if (vector < DEVICE_IRQ_FIRST || vector >= DEVICE_IRQ_FIRST + DEVICE_IRQ_COUNT) return;
    u64 flags = lock_irqs();
    __atomic_store_n(&handlers[vector - DEVICE_IRQ_FIRST].handler, 0, __ATOMIC_RELEASE);
    handlers[vector - DEVICE_IRQ_FIRST].context = 0;
    unlock_irqs(flags);
}
void device_irq_dispatch(u64 vector) {
    if (vector < DEVICE_IRQ_FIRST || vector >= DEVICE_IRQ_FIRST + DEVICE_IRQ_COUNT) return;
    const u32 i = vector - DEVICE_IRQ_FIRST;
    device_irq_handler handler = __atomic_load_n(&handlers[i].handler, __ATOMIC_ACQUIRE);
    if (handler) handler(handlers[i].context);
}
void *pci_irq_map_mmio(u64 physical, u64 length) {
    // The kernel currently uses this direct-map window for PCI MMIO.
    if (!length || physical >= (1ULL << 40) || length > (1ULL << 40) - physical) return 0;
    const u64 start = physical & ~0xfffULL;
    const u64 pages = ((physical & 0xfffULL) + length + 4095) >> 12;
    create_mapping(BASE + start, start, pages, 0x13, KernelPML4);
    flush_pages(BASE + start, pages);
    PAGING_LOOKUP_DESCRIPTOR mapping = paging_lookup(BASE + start, KernelPML4);
    if (mapping.status || mapping.Page_Type != 0 || mapping.physical_address != start) return 0;
    return (void *)(BASE + physical);
}
