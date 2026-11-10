#include "drivers/display/vga.h"
#include "drivers/usb/usb.hpp"
#include "drivers/timers/timer.hpp"
#include "x86-64/memory/memory_helpers.h"
#include "drivers/storage/ums.hpp"
#include "x86-64/memory/kmalloc.h"

static constexpr u32 MSC_CBW_SIGNATURE = 0x43425355;
static constexpr u32 MSC_CSW_SIGNATURE = 0x53425355;
static constexpr u8 MSC_DIRECTION_IN = 0x80;
static constexpr u8 MSC_DIRECTION_OUT = 0x00;

extern u64 usb_addr;
extern u64 timeradd;

typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 data_transfer_length;
    u8 flags;
    u8 lun;
    u8 command_length;
    u8 command[16];
} msc_cbw;


typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 residue;
    u8 status;
} msc_csw;

typedef struct __attribute__((packed)) {
    u8 pDeviceType;
    u8 Removable;
    u8 Version;
    u8 response_data_format;
    u8 adittional_length;
    u8 flags[3];
    char vendor[8];
    char product[16];
    char Revision[4];
} scsi_inquiry;


usb_mass_storage *drives = nullptr;


bool ums_prepare_cbw(
    msc_cbw &cbw,
    usb_mass_storage &ums,
    const u8 *cdb,
    u8 cdb_length,
    u32 data_length,
    bool device_to_host
) {
    if (!cdb || cdb_length == 0 || cdb_length > 16)
        return false;

    if (ums.lun > 15)
        return false;

    memset(&cbw, 0, sizeof(cbw));

    cbw.signature = MSC_CBW_SIGNATURE;

    cbw.tag = ++ums.next_tag;

    cbw.data_transfer_length = data_length;

    cbw.flags = data_length && device_to_host
        ? MSC_DIRECTION_IN
        : MSC_DIRECTION_OUT;

    cbw.lun = ums.lun;

    cbw.command_length = cdb_length;

    memcpy(cbw.command, cdb, cdb_length);

    return true;
}


void ums_prepare_csw(msc_csw &csw) {
    memset(&csw, 0, sizeof(csw));
}

bool ums_validate_csw(
    const msc_csw &csw,
    const msc_cbw &cbw
) {
    if (csw.signature != MSC_CSW_SIGNATURE)
        return false;

    if (csw.tag != cbw.tag)
        return false;

    if (csw.residue > cbw.data_transfer_length)
        return false;

    if (csw.status > 2)
        return false;

    return true;
}


bool ums_prepare_data_buffer(
    void *buffer,
    u32 length
) {
    if (length == 0)
        return true;

    if (!buffer)
        return false;

    return true;
}


u8 ums_command(usb_mass_storage *drive, const u8 *cdb, u8 cdb_length, void *data, u32 data_length, bool device_to_host, u32 timeout) {
    msc_cbw cbw = {};
    msc_csw csw = {};
    
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;

    if (!ums_prepare_data_buffer(data, data_length))
        return 1;

    if (!ums_prepare_cbw(cbw, *drive, cdb, cdb_length, data_length, device_to_host))
        return 1;

    ums_prepare_csw(csw);

    usb_transfer_result result = drive->controller->usb_transfer(drive->endpoint_out_addr, &cbw, sizeof(cbw), timer_source, timeout);

    if (result.status_code != TRANSFER_SUCCESS || result.actual_length != sizeof(cbw))
        return 1;


    
    if (data_length > 0) {
        u32 endpoint = device_to_host
            ? drive->endpoint_in_addr
            : drive->endpoint_out_addr;

        result = drive->controller->usb_transfer(
            endpoint,
            data,
            data_length,
            timer_source,
            timeout
        );

        if (result.status_code != TRANSFER_SUCCESS)
            return 1;

        // Save result.actual_length for residue validation.
    }

    result = drive->controller->usb_transfer(
        drive->endpoint_in_addr,
        &csw,
        sizeof(csw),
        timer_source,
        timeout
    );

    if (result.status_code != TRANSFER_SUCCESS ||
        result.actual_length != sizeof(csw))
        return 1;
        
    if (!ums_validate_csw(csw, cbw))
        return 1;

    if (csw.status == 0)
        return 0;
    else 
        return 1;

}


static inline u64 USB_make_address(usb_mass_storage *drive) {
    u64 address = ((drive->endpoint_in_addr & ~0xFF) >> 8);
    u64 index = (u64(drive->controller->controller.index) << 16);
    u64 type_index = u64(drive->controller->controller_type - 1) << 24;
    return (address | index | type_index);
}

bool ums_init(usb_mass_storage *drive) {
    drive->initialized = false;

    u8 inquiry_buffer[36] = {0};

    u8 cdb0[6] = {
        0x12,
        0x00,
        0x00,
        0x00,
        0x24,
        0x00
    };

    // INQUIRY(6)
    if (ums_command(drive, cdb0, 6, inquiry_buffer, 36, true, 5000) != 0) {
        printf("UMS: inquiry failed\n");
        return false;
    }

    u8 cdb1[6] = { 0x00, 0, 0, 0, 0, 0 };

    bool try_again = true;
    again:
    // TEST UNIT(6)
    if (ums_command(drive, cdb1, 6, nullptr, 0, false, 5000) != 0) {
        u8 cdb2[6] = { 0x03, 0x00, 0x00, 0x00, 18, 0x00 };
        u8 sense[18] = {};

        printf("UMS: test unit ready failed, requesting sense\n");

        if (ums_command(drive, cdb2, 6, sense, 18, true, 5000) != 0) {
            printf("UMS: request sense failed\n");
            return false;
        }

        u8 sense_key = sense[2] & 0x0f;
        u8 asc = sense[12];
        u8 ascq = sense[13];
        if (sense_key == 0x02 && asc == 0x04 && ascq == 0x01) {
            if (try_again) {
                try_again = false;
                printf("UMS: trying test unit again\n");
                goto again;
            }
        } else {
            printf("UMS: test unit failed\n");
            return false;
        }
    }

    // READ CAPACITY(10)
    u8 cdb3[10] = {0}; // make everything zero
    cdb3[0] = 0x25; // only the first cell has a number, the opcode, rest is 0

    u8 capacity[8] = {};
    if (ums_command(drive, cdb3, 10, capacity, 8, true, 5000) != 0) {
        printf("UMS: READ CAPACITY(10) failed\n");
        return false;
    }

    u32 last_lba = 
        ((u32)capacity[0] << 24) |
        ((u32)capacity[1] << 16) |
        ((u32)capacity[2] << 8) |
        ((u32)capacity[3]);

    u32 block_size =
        ((u32)capacity[4] << 24) |
        ((u32)capacity[5] << 16) |
        ((u32)capacity[6] << 8) |
        ((u32)capacity[7]);

    drive->block_count = last_lba + 1;
    drive->block_size = block_size;

    scsi_inquiry *inquiry = (scsi_inquiry *)inquiry_buffer;  
    u64 device_address = USB_make_address(drive);
    printf("UMS: USB mass storage device discovered\n");
    char vendor[9] = {};
    char revision[5] = {};
    memcpy(vendor, inquiry->vendor, sizeof(inquiry->vendor));
    memcpy(revision, inquiry->Revision, sizeof(inquiry->Revision));
    printf("USB handle: 0x%lx  vendor: %s\n", device_address, vendor);
    printf("Revision: %s  Block_size: %u  block_count: %u\n",
           revision, block_size, drive->block_count);
    drive->initialized = true;
    return true;
}

u8 sector[512] = {};

void read_first_sector(usb_mass_storage *drive) {
    u8 cdb[10] = {
        0x28, // READ(10)
        0x00,
        0x00, 0x00, 0x00, 0x00, // LBA = 0
        0x00,
        0x00, 0x01,             // Transfer 1 block
        0x00
    };

    ums_command(drive, cdb, 10, sector, 512, true, 5000);
}

u16 usb_drive_count = 0;
u16 current_drive = 0;

void ums_discover() {
    GlobalUSB *USB = (GlobalUSB *)usb_addr;

    

    usb_interface_return result = USB->find_interface(0x08, 0x06, true, 0x50);
    drives = new usb_mass_storage[result.interface_count];
    usb_drive_count = result.interface_count;

    for (u16 i = 0; i < result.interface_count; i++) {
        drives[i] = {};
        drives[i].controller = result.interfaces[i].controller;
        u32 port_index = (result.interfaces[i].port_index << 16);
        u32 interface_index = i << 8;
        u32 endpoint_in = result.interfaces[i].endpoint_bulk_in;
        u32 endpoint_out = result.interfaces[i].endpoint_bulk_out;
        drives[i].endpoint_in_addr = port_index | interface_index | endpoint_in;
        drives[i].endpoint_out_addr = port_index | interface_index | endpoint_out;
    }

    
}

void UMS_drive::init() {
    drive = &drives[current_drive];
    memcpy(vendor, "UMS ", sizeof("UMS "));
    initialized = ums_init(drive);
    if (!initialized) {
        printf("UMS: failed to initialize drive 0x%w\n", current_drive);
    }
    current_drive++;
}

void free_drives() {
    kfree(drives);
}

void ums_read(usb_mass_storage *drive, u32 lba, u16 sectors, void *buffer) {


    u8 cdb[10] = {
        0x28, // READ(10)
        0x00,
        (lba >> 24), (lba >> 16), (lba >> 8), lba, // LBA = 0
        0x00,
        (sectors>>8), sectors,             
        0x00
    };

    ums_command(drive, cdb, 10, buffer, (drive->block_size*sectors), true, 60000);
}



void ums_write(usb_mass_storage *drive, u32 lba, u16 sectors, void *buffer) {

    u8 cdb[10] = {
        0x2A, // READ(10)
        0x00,
        (lba >> 24), (lba >> 16), (lba >> 8), lba, // LBA = 0
        0x00,
        (sectors>>8), sectors,             
        0x00
    };

    ums_command(drive, cdb, 10, buffer, (drive->block_size*sectors), false, 60000);
}


void UMS_drive::read_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (!drive || !buffer || sector_count == 0 || drive->block_size == 0)
        return;

    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % drive->block_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / drive->block_size
        + (covered_bytes % drive->block_size != 0);
    u64 first_block = byte_start / drive->block_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * drive->block_size);
    if (!temp)
        return;

    void *original_temp = temp;


    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    if (remainder != 0) {
        ums_read(drive, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * drive->block_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ums_read(drive, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * drive->block_size));
    }

    memcpy(buffer, static_cast<u8 *>(original_temp) + lba_remainder, byte_count);
    kfree(original_temp);
}

static inline void temp_read(usb_mass_storage *drive, u32 lba, u32 iterations, u32 remainder, void *temp) {
    if (remainder != 0) {
        ums_read(drive, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * drive->block_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ums_read(drive, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * drive->block_size));
    }

}

void UMS_drive::write_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (!drive || !buffer || sector_count == 0 || drive->block_size == 0)
        return;

    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % drive->block_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / drive->block_size
        + (covered_bytes % drive->block_size != 0);
    u64 first_block = byte_start / drive->block_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * drive->block_size);
    if (!temp)
        return;
    
    void *original_temp = temp;
    
    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    u64 RemSize = 0;
    u64 usb_size = blocks * drive->block_size;
    u64 legacy_size = sector_count << 9;
    if (usb_size > legacy_size)
        RemSize = usb_size - legacy_size;

    if (RemSize) {
        temp_read(drive, (u32)lba, iterations, remainder, temp);
    }

    memcpy(static_cast<u8 *>(temp) + lba_remainder, buffer, byte_count);

    if (remainder != 0) {
        ums_write(drive, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * drive->block_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ums_write(drive, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * drive->block_size));
    }

    kfree(original_temp);
}
