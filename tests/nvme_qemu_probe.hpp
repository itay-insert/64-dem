#pragma once

// Only compiled into the disposable QEMU test build. The attached NVMe image
// must be scratch media; this probe writes sectors 7 through 9 and restores them.
#include "drivers/storage/nvme.hpp"
#include "x86-64/memory/memory_helpers.h"
#include "drivers/display/vga.h"

static void nvme_qemu_probe(storage &disks) {
    for (u32 i = 0; i < disks.drive_count; ++i) {
        disk_drive *base = (disk_drive *)((u8 *)disks.drives + disks.offsets[i]);
        if (memcmp(base->vendor, "NVMe", 4) != 0) continue;
        NVMe_drive *drive = (NVMe_drive *)base;
        u8 before[1536] = {};
        u8 pattern[1536] = {};
        u8 after[1536] = {};
        u8 neighbors_before[1024] = {};
        u8 neighbors_after[1024] = {};
        for (u32 n = 0; n < sizeof(pattern); ++n)
            pattern[n] = (u8)(n * 37u + 11u);
        if (!drive->try_read_sectors(7, 3, before)) {
            qemu_debug_print("[NVME-TEST] initial read failed\n");
            return;
        }
        if (!drive->try_read_sectors(6, 1, neighbors_before) ||
            !drive->try_read_sectors(10, 1, neighbors_before + 512)) {
            qemu_debug_print("[NVME-TEST] adjacent read failed\n");
            return;
        }
        if (!drive->try_write_sectors(7, 3, pattern)) {
            qemu_debug_print("[NVME-TEST] write failed\n");
            return;
        }
        bool read_ok = drive->try_read_sectors(7, 3, after);
        bool neighbors_ok = drive->try_read_sectors(6, 1, neighbors_after) &&
            drive->try_read_sectors(10, 1, neighbors_after + 512);
        bool match = read_ok && neighbors_ok &&
            memcmp(pattern, after, sizeof(pattern)) == 0 &&
            memcmp(neighbors_before, neighbors_after, sizeof(neighbors_before)) == 0;
        bool restored = drive->try_write_sectors(7, 3, before);
        if (match && restored) qemu_debug_print("[NVME-TEST] PASS\n");
        else qemu_debug_print("[NVME-TEST] FAIL\n");
        return;
    }
    qemu_debug_print("[NVME-TEST] no NVMe drive registered\n");
}
