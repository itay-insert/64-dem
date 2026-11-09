#pragma once

#include "uint_definitions.h"
#include "drivers/storage/storage.hpp"
#include "drivers/usb/usb.hpp"

typedef struct {
    u32 endpoint_in_addr;
    u32 endpoint_out_addr;
    usb_controller *controller;
    bool initialized;
    u8 lun;
    u32 next_tag;
    u32 block_size;
    u32 block_count;
} usb_mass_storage;

class UMS_drive : public disk_drive {
    private:
        usb_mass_storage *drive;

    public:
        void read_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void write_sectors(u32 lba, u32 sector_count, void *buffer) override;
        void init() override;
};