#ifndef STORAGE_H
#define STORAGE_H

typedef struct {
    u64 ops;
    u64 driver_data;
    u32 logical_block_size;
    u32 physical_block_size;
    u32 max_blocks_per_request;
    u32 flags;
} block_device_t;

#endif