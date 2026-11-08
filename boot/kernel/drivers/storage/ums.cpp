#include "drivers/display/vga.h"
#include "drivers/usb/usb.hpp"
#include "drivers/timers/timer.hpp"

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

usb_mass_storage *drives = nullptr;

void ums_init() {
    GlobalUSB *USB = (GlobalUSB *)usb_addr;

    

    usb_interface_return result = USB->find_interface(0x08, 0x06, true, 0x50);
    drives = new usb_mass_storage[result.interface_count];


    
}


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

    cbw.tag = ++msc.next_tag;

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
    
    Timer timer = (Timer *)timeradd;
    TimerSource &Timer = timer->Src;

    if (!ums_prepare_data_buffer(data, data_length))
        return 1;

    if (!ums_prepare_cbw(cbw, drive, cdb, cdb_length, data_length, device_to_host))
        return 1;

    if (!ums_prepare_csw(csw, drive, cdb, cdb_length, data_length, device_to_host))
        return 1;

    usb_transfer_result result = drive->controller->usb_transfer(drive->endpoint_out_addr, &cbw, sizeof(cbw), Timer, timeout);

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
            Timer,
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
        Timer,
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

bool ums_init(usb_mass_storage *drive) {
    drive->initialized = false;



}

void ums_write(usb_mass_storage *drive, u32 lba, u32 sectors, void *buffer) {
    Timer timer = (Timer *)timeradd;
    TimerSource &Timer = timer->Src;

    drive->controller->usb_transfer(drive->endpoint_out_addr, &cbw, sizeof(cbw), Timer)
}