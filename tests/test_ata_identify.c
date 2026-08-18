#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "uint_definitions.h"

enum mock_command {
    MOCK_COMMAND_NONE,
    MOCK_COMMAND_ATA,
    MOCK_COMMAND_ATAPI,
    MOCK_COMMAND_WRITE,
    MOCK_COMMAND_FLUSH,
    MOCK_COMMAND_READ
};

static const u16 mock_io = 0x1F0;
static const u16 mock_control = 0x3F6;
static enum mock_command mock_command;
static u8 mock_signature_mid;
static u8 mock_signature_high;
static u8 mock_completion_status;
static u16 mock_identify[256];
static u32 mock_status_reads;
static u32 mock_data_reads;
static u32 mock_data_writes;
static u8 mock_write_command;
static u8 mock_flush_command;
static u8 mock_read_command;
static u16 mock_written_data[512];

u8 inb(u16 port);
void outb(u16 port, u8 byte);
u16 inw(u16 port);
void outw(u16 port, u16 word);
void io_wait(void);
u32 inl(u16 port);
void outl(u16 port, u32 value);

#define PORTS_H
#include "../boot/kernel/drivers/storage/ide/atahlp.c"
#include "../boot/kernel/drivers/storage/ide/ataid.c"
#include "../boot/kernel/drivers/storage/ide/ata.c"

static void mock_reset(u8 signature_mid, u8 signature_high) {
    mock_command = MOCK_COMMAND_NONE;
    mock_signature_mid = signature_mid;
    mock_signature_high = signature_high;
    mock_completion_status = ATA_SR_DRQ | ATA_SR_DRDY;
    mock_status_reads = 0;
    mock_data_reads = 0;
    mock_data_writes = 0;
    mock_write_command = 0;
    mock_flush_command = 0;
    mock_read_command = 0;
    memset(mock_identify, 0, sizeof(mock_identify));
}

u8 inb(u16 port) {
    if (port == mock_control)
        return ATA_SR_DRDY;
    if (port == mock_io + ATA_REG_LBA_MID)
        return mock_signature_mid;
    if (port == mock_io + ATA_REG_LBA_HIGH)
        return mock_signature_high;
    if (port == mock_io + ATA_REG_STATUS) {
        mock_status_reads++;
        if (mock_command != MOCK_COMMAND_NONE && mock_status_reads == 2)
            return ATA_SR_BSY;
        return mock_command == MOCK_COMMAND_NONE ? ATA_SR_DRDY
                                                 : mock_completion_status;
    }
    return 0;
}

void outb(u16 port, u8 byte) {
    if (port != mock_io + ATA_REG_COMMAND)
        return;
    if (byte == ATA_CMD_IDENTIFY)
        mock_command = MOCK_COMMAND_ATA;
    else if (byte == ATA_CMD_IDENTIFY_PACKET) {
        mock_command = MOCK_COMMAND_ATAPI;
        mock_completion_status = ATA_SR_DRQ | ATA_SR_DRDY;
    } else if (byte == ATA_CMD_WRITE || byte == ATA_CMD_WRITE_EXT) {
        mock_command = MOCK_COMMAND_WRITE;
        mock_write_command = byte;
        mock_completion_status = ATA_SR_DRQ | ATA_SR_DRDY;
    } else if (byte == ATA_CMD_FLUSH || byte == ATA_CMD_FLUSH_EXT) {
        mock_command = MOCK_COMMAND_FLUSH;
        mock_flush_command = byte;
        mock_completion_status = ATA_SR_DRDY;
    } else if (byte == ATA_CMD_READ || byte == ATA_CMD_READ_EXT) {
        mock_command = MOCK_COMMAND_READ;
        mock_read_command = byte;
        mock_completion_status = ATA_SR_DRQ | ATA_SR_DRDY;
    }
}

u16 inw(u16 port) {
    assert(port == mock_io + ATA_REG_DATA);
    assert(mock_data_reads < 256);
    return mock_identify[mock_data_reads++];
}

void outw(u16 port, u16 word) {
    assert(port == mock_io + ATA_REG_DATA);
    assert(mock_data_writes < 512);
    mock_written_data[mock_data_writes++] = word;
}
void io_wait(void) {}
u32 inl(u16 port) { (void)port; return 0; }
void outl(u16 port, u32 value) { (void)port; (void)value; }

static ata_drive_t new_drive(void) {
    ata_drive_t drive = {0};
    static ata_channel_t mock_channel;
    mock_channel.io_base = mock_io;
    mock_channel.control_port = mock_control;
    drive.channel = &mock_channel;
    return drive;
}

static void set_identify_string(u32 first_word, u32 word_count,
                                const char *text) {
    size_t length = strlen(text);
    for (u32 i = 0; i < word_count; i++) {
        size_t first = (size_t)i * 2;
        u8 high = first < length ? (u8)text[first] : (u8)' ';
        u8 low = first + 1 < length ? (u8)text[first + 1] : (u8)' ';
        mock_identify[first_word + i] = ((u16)high << 8) | low;
    }
}

static void test_ata_lba48_identification(void) {
    mock_reset(0x00, 0x00);
    mock_identify[49] = 1u << 9;
    mock_identify[83] = 0x4000 | (1u << 10);
    mock_identify[60] = 0x1234;
    mock_identify[61] = 0x0001;
    mock_identify[100] = 0x5678;
    mock_identify[101] = 0x1234;
    set_identify_string(10, 10, "SERIAL-ATA");
    set_identify_string(27, 20, "MOCK ATA DISK");

    ata_drive_t drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_ATA);
    assert(drive.type == ATA_DEVICE_ATA);
    assert(drive.supports_lba28);
    assert(drive.supports_lba48);
    assert(drive.sector_count == UINT64_C(0x12345678));
    assert(strncmp(drive.serial, "SERIAL-ATA", 10) == 0);
    assert(strncmp(drive.model, "MOCK ATA DISK", 13) == 0);
    assert(mock_command == MOCK_COMMAND_ATA);
    assert(mock_data_reads == 256);
}

static void test_ata_lba28_fallback(void) {
    mock_reset(0x00, 0x00);
    mock_identify[49] = 1u << 9;
    mock_identify[60] = 0xBEEF;
    mock_identify[61] = 0x0002;
    ata_drive_t drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_ATA);
    assert(!drive.supports_lba48);
    assert(drive.sector_count == UINT64_C(0x2BEEF));
}

static void test_atapi_identification(void) {
    mock_reset(0x14, 0xEB);
    mock_completion_status = ATA_SR_ERR;
    mock_identify[0] = 1u << 15;
    set_identify_string(10, 10, "SERIAL-CD");
    set_identify_string(27, 20, "MOCK ATAPI CDROM");
    ata_drive_t drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_ATAPI);
    assert(drive.type == ATA_DEVICE_ATAPI);
    assert(!drive.supports_lba28 && !drive.supports_lba48);
    assert(drive.sector_count == 0);
    assert(strncmp(drive.model, "MOCK ATAPI CDROM", 16) == 0);
    assert(mock_command == MOCK_COMMAND_ATAPI);
    assert(mock_data_reads == 256);
}

static void test_probe_rejects_invalid_atapi_signature(void) {
    mock_reset(0x00, 0x00);
    mock_completion_status = ATA_SR_ERR;
    ata_drive_t drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_PROTOCOL_ERROR);
    assert(drive.type == ATA_DEVICE_NONE);
    assert(mock_data_reads == 0);
}

static void test_probe_error_paths(void) {
    assert(ata_probe_device(NULL) == ATA_PROBE_PROTOCOL_ERROR);

    mock_reset(0x00, 0x00);
    mock_completion_status = 0;
    ata_drive_t drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_NONE);
    assert(drive.type == ATA_DEVICE_NONE);
    assert(drive.channel->lock.locked == 0);

    mock_reset(0x00, 0x00);
    mock_completion_status = ATA_SR_DF;
    drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_DEVICE_FAULT);
    assert(drive.type == ATA_DEVICE_NONE);
    assert(drive.channel->lock.locked == 0);

    mock_reset(0x00, 0x00);
    mock_completion_status = ATA_SR_DRDY;
    drive = new_drive();
    assert(ata_probe_device(&drive) == ATA_PROBE_PROTOCOL_ERROR);
    assert(drive.type == ATA_DEVICE_NONE);
    assert(drive.channel->lock.locked == 0);
}

static void test_channel_reset(void) {
    mock_reset(0x00, 0x00);
    assert(ata_reset_channel(mock_control));
}

static void test_ata_lba28_write_and_flush(void) {
    mock_reset(0x00, 0x00);

    ata_drive_t drive = new_drive();
    drive.type = ATA_DEVICE_ATA;
    drive.supports_lba28 = true;
    drive.sector_count = 1024;

    u8 buffer[512];
    for (u32 i = 0; i < sizeof(buffer); i++)
        buffer[i] = (u8)i;

    ata_write(&drive, 7, buffer, 1);

    assert(drive.ata_status == SUCCESS);
    assert(mock_write_command == ATA_CMD_WRITE);
    assert(mock_flush_command == ATA_CMD_FLUSH);
    assert(mock_data_writes == 256);
    assert(mock_written_data[0] == 0x0100);
    assert(mock_written_data[255] == 0xFFFE);
}

static void test_ata_lba48_write_and_flush(void) {
    mock_reset(0x00, 0x00);

    ata_drive_t drive = new_drive();
    drive.type = ATA_DEVICE_ATA;
    drive.supports_lba28 = true;
    drive.supports_lba48 = true;
    drive.sector_count = (1ULL << 28) + 1024;

    u8 buffer[512] = {0};
    ata_write(&drive, 1ULL << 28, buffer, 1);

    assert(drive.ata_status == SUCCESS);
    assert(mock_write_command == ATA_CMD_WRITE_EXT);
    assert(mock_flush_command == ATA_CMD_FLUSH_EXT);
    assert(mock_data_writes == 256);
}

static void test_ata_lba28_read(void) {
    mock_reset(0x00, 0x00);

    ata_drive_t drive = new_drive();
    drive.type = ATA_DEVICE_ATA;
    drive.supports_lba28 = true;
    drive.sector_count = 1024;

    for (u32 i = 0; i < 256; i++)
        mock_identify[i] = (u16)(0xA000 + i);

    u8 buffer[512] = {0};
    ata_read(&drive, 9, buffer, 1);

    assert(drive.ata_status == SUCCESS);
    assert(mock_read_command == ATA_CMD_READ);
    assert(mock_data_reads == 256);
    assert(buffer[0] == 0x00 && buffer[1] == 0xA0);
    assert(buffer[510] == 0xFF && buffer[511] == 0xA0);
}

static void test_ata_lba48_read(void) {
    mock_reset(0x00, 0x00);

    ata_drive_t drive = new_drive();
    drive.type = ATA_DEVICE_ATA;
    drive.supports_lba48 = true;
    drive.sector_count = (1ULL << 28) + 1024;

    u8 buffer[512];
    ata_read(&drive, 1ULL << 28, buffer, 1);

    assert(drive.ata_status == SUCCESS);
    assert(mock_read_command == ATA_CMD_READ_EXT);
    assert(mock_data_reads == 256);
}

int main(void) {
    test_ata_lba48_identification();
    test_ata_lba28_fallback();
    test_atapi_identification();
    test_probe_rejects_invalid_atapi_signature();
    test_probe_error_paths();
    test_channel_reset();
    test_ata_lba28_write_and_flush();
    test_ata_lba48_write_and_flush();
    test_ata_lba28_read();
    test_ata_lba48_read();
    puts("ATA identify tests passed");
    return 0;
}
