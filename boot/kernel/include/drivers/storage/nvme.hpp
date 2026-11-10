#pragma once

#include "drivers/storage/storage.hpp"
#include "drivers/timers/timer.hpp"

// A polling, one-command-at-a-time NVMe PCIe driver.
void nvme_discover(TimerSource &timer);
u32 nvme_namespace_count();

class NVMe_drive : public disk_drive {
    u32 namespace_index;

public:
    explicit NVMe_drive(u32 index) : namespace_index(index) {}
    void init() override;
    bool try_read_sectors(u32 lba, u32 sector_count, void *buffer);
    bool try_write_sectors(u32 lba, u32 sector_count, const void *buffer);
    void read_sectors(u32 lba, u32 sector_count, void *buffer) override;
    void write_sectors(u32 lba, u32 sector_count, void *buffer) override;
};
