#include "drivers/storage/nvme.hpp"
#include "drivers/pci/pci.h"
#include "drivers/display/vga.h"
#include "x86-64/paging.h"
#include "x86-64/memory/dma.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/spinlock.h"

namespace {

constexpr u32 page_size = 4096;
constexpr u16 queue_depth = 16;
constexpr u32 max_controllers = 8;
constexpr u32 max_namespaces = 32;
constexpr u32 command_timeout_ms = 5000;

struct command {
    u8 opcode;
    u8 flags;
    u16 cid;
    u32 nsid;
    u64 reserved;
    u64 metadata;
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
} __attribute__((packed));
static_assert(sizeof(command) == 64, "NVMe command must be 64 bytes");

struct completion {
    u32 result;
    u32 reserved;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;
} __attribute__((packed));
static_assert(sizeof(completion) == 16, "NVMe completion must be 16 bytes");

struct queue {
    dma_ret submission;
    dma_ret completion_dma;
    u16 tail;
    u16 head;
    u16 next_cid;
    u8 phase;
};

struct controller {
    pci_address_t pci;
    volatile u8 *mmio;
    TimerSource *timer;
    queue admin;
    queue io;
    dma_ret data;
    spinlock_t lock;
    u32 stride;
    bool ready;
};

struct namespace_info {
    controller *ctrl;
    u32 id;
    u32 block_size;
    u64 blocks;
};

controller controllers[max_controllers] = {};
namespace_info namespaces[max_namespaces] = {};
u32 controller_count = 0;
u32 namespace_count = 0;
bool discovered = false;

u16 le16(const u8 *p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

u32 le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

u64 le64(const u8 *p) {
    return (u64)le32(p) | ((u64)le32(p + 4) << 32);
}

u32 reg32(controller &ctrl, u32 offset) {
    return *(volatile u32 *)(ctrl.mmio + offset);
}

u64 reg64(controller &ctrl, u32 offset) {
    u32 low = reg32(ctrl, offset);
    return (u64)low | ((u64)reg32(ctrl, offset + 4) << 32);
}

void write32(controller &ctrl, u32 offset, u32 value) {
    *(volatile u32 *)(ctrl.mmio + offset) = value;
}

void write64(controller &ctrl, u32 offset, u64 value) {
    write32(ctrl, offset, (u32)value);
    write32(ctrl, offset + 4, (u32)(value >> 32));
}

bool wait_ready(controller &ctrl, bool expected, u32 timeout_ms) {
    for (u32 elapsed = 0; elapsed <= timeout_ms; ++elapsed) {
        u32 status = reg32(ctrl, 0x1c);
        if (expected && (status & 2)) return false; // controller fatal status
        if (((status & 1) != 0) == expected) return true;
        if (elapsed != timeout_ms) wait_ms(*ctrl.timer, 1);
    }
    return false;
}

bool allocate_queue(queue &q) {
    q.submission = allocate_dma(page_size);
    if (q.submission.status) return false;
    q.completion_dma = allocate_dma(page_size);
    if (q.completion_dma.status) return false;
    memset((void *)q.submission.virtual_address, 0, page_size);
    memset((void *)q.completion_dma.virtual_address, 0, page_size);
    q.phase = 1;
    return true;
}

// A timed-out queue is never reused: a late completion could otherwise be
// mistaken for a later command, or DMA into a recycled buffer.
bool submit(controller &ctrl, queue &q, u16 qid, command &cmd) {
    if (!ctrl.ready) return false;
    u16 cid = ++q.next_cid;
    cmd.cid = cid;
    command *sq = (command *)q.submission.virtual_address;
    sq[q.tail] = cmd;
    q.tail = (q.tail + 1) % queue_depth;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    write32(ctrl, 0x1000 + (2u * qid) * ctrl.stride, q.tail);

    volatile completion *cq = (volatile completion *)q.completion_dma.virtual_address;
    for (u32 elapsed = 0; elapsed <= command_timeout_ms; ++elapsed) {
        volatile completion &entry = cq[q.head];
        u16 status = entry.status;
        if ((status & 1) == q.phase) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            u16 completed_cid = entry.cid;
            u16 completed_qid = entry.sq_id;
            q.head = (q.head + 1) % queue_depth;
            if (q.head == 0) q.phase ^= 1;
            write32(ctrl, 0x1000 + (2u * qid + 1u) * ctrl.stride, q.head);
            if (completed_cid != cid || completed_qid != qid || (status >> 1)) {
                if (completed_cid != cid || completed_qid != qid) ctrl.ready = false;
                return false;
            }
            return true;
        }
        if (reg32(ctrl, 0x1c) & 2) break;
        if (elapsed != command_timeout_ms) wait_ms(*ctrl.timer, 1);
    }
    ctrl.ready = false;
    printf("NVMe: command timed out or controller failed\n");
    return false;
}

bool identify(controller &ctrl, u32 nsid, u8 cns) {
    memset((void *)ctrl.data.virtual_address, 0, page_size);
    command cmd = {};
    cmd.opcode = 0x06;
    cmd.nsid = nsid;
    cmd.prp1 = ctrl.data.physical_address;
    cmd.cdw10 = cns;
    return submit(ctrl, ctrl.admin, 0, cmd);
}

bool create_io_queues(controller &ctrl) {
    command cq = {};
    cq.opcode = 0x05;
    cq.prp1 = ctrl.io.completion_dma.physical_address;
    cq.cdw10 = 1u | ((u32)(queue_depth - 1) << 16);
    cq.cdw11 = 1u; // physically contiguous, interrupt disabled
    if (!submit(ctrl, ctrl.admin, 0, cq)) return false;

    command sq = {};
    sq.opcode = 0x01;
    sq.prp1 = ctrl.io.submission.physical_address;
    sq.cdw10 = 1u | ((u32)(queue_depth - 1) << 16);
    sq.cdw11 = 1u | (1u << 16); // physically contiguous; CQ ID 1
    return submit(ctrl, ctrl.admin, 0, sq);
}

bool register_namespace(controller &ctrl, u32 id) {
    if (!identify(ctrl, id, 0)) return false;
    const u8 *data = (const u8 *)ctrl.data.virtual_address;
    u64 blocks = le64(data);
    u8 format = (data[26] & 0x0f) | ((data[26] & 0x60) >> 1);
    if (format > data[25]) return false;
    const u8 *lbaf = data + 128 + 4u * format;
    u16 metadata = le16(lbaf);
    u8 shift = lbaf[2];
    if (!blocks || metadata || data[29] || shift < 9 || shift > 12)
        return false; // current block API supports 512..4096 byte data-only LBAs
    namespaces[namespace_count++] = {&ctrl, id, 1u << shift, blocks};
    printf("NVMe: namespace %u, %lu blocks of %u bytes\n", id, blocks, 1u << shift);
    return true;
}

void enumerate_namespaces(controller &ctrl) {
    if (!identify(ctrl, 0, 1)) return;
    u32 reported = le32((const u8 *)ctrl.data.virtual_address + 516);
    if (!reported) return;
    u32 cursor = 0;
    while (namespace_count < max_namespaces && ctrl.ready) {
        if (!identify(ctrl, cursor, 2)) return;
        u32 ids[1024];
        const u8 *data = (const u8 *)ctrl.data.virtual_address;
        for (u32 i = 0; i < 1024; ++i) ids[i] = le32(data + i * 4);
        bool more = false;
        for (u32 i = 0; i < 1024 && namespace_count < max_namespaces; ++i) {
            if (!ids[i]) return;
            if (ids[i] <= cursor) return; // reject malformed or repeated list
            cursor = ids[i];
            register_namespace(ctrl, cursor);
            more = (i == 1023);
            if (!ctrl.ready) return;
        }
        if (!more) return;
    }
}

bool initialize(controller &ctrl, const PCI_ret &device, TimerSource &timer) {
    ctrl.timer = &timer;
    ctrl.pci = {device.Segment, device.Bus, device.Device, device.Function};
    pci_bar_t bar = {};
    if (pci_read_bar(ctrl.pci, 0, 1, &bar) != PCI_STATUS_SUCCESS ||
        bar.type != PCI_BAR_MEMORY || !bar.address || !bar.size) return false;
    if (pci_set_command_bits(ctrl.pci,
            PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER,
            PCI_COMMAND_INTX_DISABLE) != PCI_STATUS_SUCCESS) return false;

    u64 page = bar.address & ~4095ULL;
    u64 offset = bar.address - page;
    u64 initial_pages = (offset + 0x1000 + 4095) / 4096;
    create_mapping(page + BASE, page, initial_pages, 0x13, KernelPML4);
    flush_pages(page + BASE, initial_pages);
    ctrl.mmio = (volatile u8 *)(bar.address + BASE);

    u64 cap = reg64(ctrl, 0);
    if (!(cap & (1ull << 37)) || ((cap >> 48) & 0xf)) return false;
    ctrl.stride = 4u << ((cap >> 32) & 0xf);
    u64 required = 0x1000ull + 4ull * ctrl.stride;
    if (bar.size < required || (cap & 0xffff) + 1 < queue_depth) return false;
    u64 pages = (offset + required + 4095) / 4096;
    create_mapping(page + BASE, page, pages, 0x13, KernelPML4);
    flush_pages(page + BASE, pages);

    u32 timeout = (u32)((cap >> 24) & 0xff) * 500;
    if (!timeout) timeout = command_timeout_ms;
    write32(ctrl, 0x14, reg32(ctrl, 0x14) & ~1u);
    if (!wait_ready(ctrl, false, timeout)) return false;
    if (!allocate_queue(ctrl.admin) || !allocate_queue(ctrl.io)) return false;
    ctrl.data = allocate_dma(page_size);
    if (ctrl.data.status) return false;

    write32(ctrl, 0x24, (queue_depth - 1) | ((u32)(queue_depth - 1) << 16));
    write64(ctrl, 0x28, ctrl.admin.submission.physical_address);
    write64(ctrl, 0x30, ctrl.admin.completion_dma.physical_address);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    write32(ctrl, 0x14, (6u << 16) | (4u << 20) | 1u); // 64-byte SQE, 16-byte CQE, 4 KiB pages
    if (!wait_ready(ctrl, true, timeout)) return false;
    ctrl.ready = true;
    if (!create_io_queues(ctrl)) return false;
    enumerate_namespaces(ctrl);
    return ctrl.ready;
}

void discover_callback(const PCI_ret *device, void *context) {
    if (device->PCI_status != PCI_STATUS_SUCCESS ||
        device->common.class_code != 1 || device->common.subclass != 8 ||
        device->common.prog_if != 2 || controller_count == max_controllers) return;
    controller &ctrl = controllers[controller_count++];
    if (!initialize(ctrl, *device, *(TimerSource *)context))
        printf("NVMe: failed to initialize controller %u:%u.%u\n",
               device->Bus, device->Device, device->Function);
}

bool transfer(namespace_info &ns, u32 lba, u32 count, void *buffer, bool write) {
    if (!buffer || !count || !ns.ctrl || !ns.ctrl->ready) return false;
    u64 ratio = ns.block_size / 512;
    if (ns.blocks > (~0ull / ratio)) return false;
    u64 total = ns.blocks * ratio;
    if ((u64)lba >= total || (u64)count > total - lba) return false;

    controller &ctrl = *ns.ctrl;
    spin_lock(&ctrl.lock);
    u8 *bytes = (u8 *)buffer;
    u64 sector = lba;
    while (count && ctrl.ready) {
        u64 block = sector / ratio;
        u32 within = (u32)(sector % ratio);
        u32 sectors = (u32)ratio - within;
        if (sectors > count) sectors = count;
        u8 *bounce = (u8 *)ctrl.data.virtual_address;
        if (!write || within || sectors != ratio) {
            command read = {};
            read.opcode = 0x02;
            read.nsid = ns.id;
            read.prp1 = ctrl.data.physical_address;
            read.cdw10 = (u32)block;
            read.cdw11 = (u32)(block >> 32);
            if (!submit(ctrl, ctrl.io, 1, read)) break;
        }
        if (write) {
            memcpy(bounce + within * 512, bytes, sectors * 512);
            command cmd = {};
            cmd.opcode = 0x01;
            cmd.nsid = ns.id;
            cmd.prp1 = ctrl.data.physical_address;
            cmd.cdw10 = (u32)block;
            cmd.cdw11 = (u32)(block >> 32);
            cmd.cdw12 = 1u << 30; // force unit access for completed writes
            if (!submit(ctrl, ctrl.io, 1, cmd)) break;
        } else {
            memcpy(bytes, bounce + within * 512, sectors * 512);
        }
        bytes += sectors * 512;
        sector += sectors;
        count -= sectors;
    }
    bool success = count == 0;
    spin_unlock(&ctrl.lock);
    return success;
}

} // namespace

void nvme_discover(TimerSource &timer) {
    if (discovered) return;
    discovered = true;
    pci_enumerate(discover_callback, &timer);
}

u32 nvme_namespace_count() { return namespace_count; }

void NVMe_drive::init() {
    memcpy(vendor, "NVMe", sizeof(vendor));
    disk_type = 2;
    disk_index = (u8)namespace_index;
    initialized = namespace_index < namespace_count &&
                  namespaces[namespace_index].ctrl &&
                  namespaces[namespace_index].ctrl->ready;
    sector_size = initialized ? namespaces[namespace_index].block_size : 0;
}

bool NVMe_drive::try_read_sectors(u32 lba, u32 sector_count, void *buffer) {
    return namespace_index < namespace_count &&
           transfer(namespaces[namespace_index], lba, sector_count, buffer, false);
}

bool NVMe_drive::try_write_sectors(u32 lba, u32 sector_count, const void *buffer) {
    return namespace_index < namespace_count &&
           transfer(namespaces[namespace_index], lba, sector_count,
                    const_cast<void *>(buffer), true);
}

void NVMe_drive::read_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (!try_read_sectors(lba, sector_count, buffer))
        printf("NVMe: read failed at sector %u\n", lba);
}

void NVMe_drive::write_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (!try_write_sectors(lba, sector_count, buffer))
        printf("NVMe: write failed at sector %u\n", lba);
}
