#include "uint_definitions.h"
#include "drivers/storage/storage.h"

typedef enum {
    BLOCK_OK = 0,
    BLOCK_ERR_IO,
    BLOCK_ERR_RANGE,
    BLOCK_ERR_READ_ONLY,
    BLOCK_ERR_UNSUPPORTED,
    BLOCK_ERR_TIMEOUT
} block_status_t;



typedef struct {
    block_status_t (*read)(
        block_device_t *dev, u64 lba, u32 count, void *buffer);
    block_status_t (*write)(
        block_device_t *dev, u64 lba, u32 count, const void *buffer);
    block_status_t (*flush)(block_device_t *dev);
} block_device_ops_t;



struct block_device {
    const block_device_ops_t *ops;
    void *driver_data;

    u64 block_count;
    u32 logical_block_size;
    u32 physical_block_size;
    u32 max_blocks_per_request;
    u32 flags;
};

