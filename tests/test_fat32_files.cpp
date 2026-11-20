#include <assert.h>
#include <stddef.h>
#include "drivers/display/vga.h"
#include "filesystem/fs_flags.h"
#include "x86-64/memory/kmalloc.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/scheduler.hpp"

extern "C" void *malloc(size_t);
extern "C" void free(void *);

struct fs_stat {
    u32 mode;
    u32 reserved;
    u64 size;
    i64 mtime;
};

extern "C" int stat(const char *, fs_stat *);
extern "C" int fs_open(const char *, int, unsigned);
extern "C" i64 fs_read(int, void *, u64);
extern "C" i64 fs_write(int, const void *, u64);
extern "C" i64 fs_seek(int, i64, int);
extern "C" int fs_close(int);

BootFileSystem *fat32_fs;
static Proccess process = {};
Proccess *pid = &process;
int current_proccess;
u64 former_user_rsp;
u64 current_user_rsp;

extern "C" text_data printf(const char *, ...) { return {}; }
extern "C" u8 rtc_read(u8 reg) {
    if (reg == 9)
        return 0x26;
    return reg == 7 || reg == 8 ? 1 : 0;
}

extern "C" u8 str_size(const char *value) {
    u8 size = 0;
    while (*value++)
        ++size;
    return size;
}

extern "C" void *kmalloc(u64 size) { return malloc(size); }

extern "C" void kfree(void *ptr) {
    assert(ptr);
    free(ptr);
}

extern "C" void *krealloc(void *ptr, u64 old_size, u64 new_size) {
    auto *result = static_cast<u8 *>(malloc(new_size));
    if (!result)
        return nullptr;
    memset(result, 0, new_size);
    if (ptr) {
        memcpy(result, ptr, old_size < new_size ? old_size : new_size);
        free(ptr);
    }
    return result;
}

class MemoryDisk : public disk_drive {
public:
    static constexpr unsigned sector_count = 32;
    static constexpr unsigned sector_size_bytes = 512;
    u8 data[sector_count][sector_size_bytes] = {};

    void init() override {}
    void read_sectors(u32 lba, u32 count, void *buffer) override {
        assert(lba + count <= sector_count);
        memcpy(buffer, data[lba], count * sector_size_bytes);
    }
    void write_sectors(u32 lba, u32 count, void *buffer) override {
        assert(lba + count <= sector_count);
        memcpy(data[lba], buffer, count * sector_size_bytes);
    }

    void put16(unsigned lba, unsigned offset, u16 value) {
        memcpy(data[lba] + offset, &value, sizeof(value));
    }
    void put32(unsigned lba, unsigned offset, u32 value) {
        memcpy(data[lba] + offset, &value, sizeof(value));
    }
};

int main() {
    MemoryDisk disk;
    disk.put16(1, 11, 512);        // Bytes per sector
    disk.data[1][13] = 1;         // Sectors per cluster
    disk.put16(1, 14, 2);          // Reserved sectors
    disk.data[1][16] = 2;         // FAT copies
    disk.put32(1, 32, 31);         // Volume sectors
    disk.put32(1, 36, 1);          // Sectors per FAT
    disk.put32(1, 44, 2);          // Root cluster
    disk.put16(1, 48, 1);          // FSInfo sector
    disk.put16(1, 510, 0xAA55);
    disk.put32(2, 0, 0x41615252);
    disk.put32(2, 484, 0x61417272);
    disk.put32(2, 488, 23);
    disk.put32(2, 508, 0xAA550000);
    for (unsigned fat = 3; fat <= 4; ++fat)
        for (unsigned cluster = 2; cluster <= 4; ++cluster)
            disk.put32(fat, cluster * 4, 0x0FFFFFFF);

    FAT32_DIR shell = {}, text = {};
    memcpy(shell.filename, "SHELL      ", 11);
    shell.attributes = 0x10;
    shell.cluster_low = 3;
    memcpy(text.filename, "README  TXT", 11);
    text.cluster_low = 4;
    text.file_size = 3;
    memcpy(disk.data[5], &shell, sizeof(shell));
    memcpy(disk.data[6], &text, sizeof(text));
    memcpy(disk.data[7], "abc", 3);

    BootFileSystem fs;
    fat32_fs = &fs;
    fs.fs_init(&disk, nullptr, 1, 31 * 512);

    fs_stat metadata;
    assert(stat("/", &metadata) == 0 && (metadata.mode & 0170000) == 0040000);
    assert(stat("/shell/", &metadata) == 0 && (metadata.mode & 0170000) == 0040000);
    assert(stat("/shell/readme.txt", &metadata) == 0 && metadata.size == 3);
    assert(stat("/shell/readme.txt/", &metadata) != 0);
    assert(stat("/missing/file.txt", &metadata) != 0);
    assert(stat("/missing/../shell", &metadata) != 0);

    Path_ret cwd = fs.open_path("/SHELL/");
    assert(cwd.status);
    process.file_handle = (u64)cwd.index << 32;
    assert(stat("./readme.txt", &metadata) == 0 && metadata.size == 3);
    assert(stat("../shell", &metadata) == 0 && (metadata.mode & 0170000) == 0040000);
    assert(fs_open("missing.txt", RD_ONLY, 0) == -FS_OPEN_ERR_NO_ENTRY);
    assert(fs_open("/shell", RD_ONLY, 0) == -FS_OPEN_ERR_DIRECTORY);
    assert(fs_open("readme.txt/", RD_ONLY, 0) == -FS_OPEN_ERR_NOT_DIR);

    const int create = FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE;
    int file = fs_open("new.txt", RD_WR | create, 0666);
    assert(file >= 0);
    assert(fs_open("/shell/new.txt", WR_ONLY | create, 0666) == -FS_OPEN_ERR_EXISTS);
    assert(stat("new.txt", &metadata) == 0 && metadata.size == 0);
    assert(fs_seek(file, 512, SEEK_SET) == 512);
    assert(fs_write(file, "one", 3) == 3);
    assert(stat("/shell/new.txt", &metadata) == 0 && metadata.size == 515);
    int append = fs_open("new.txt", WR_ONLY | FS_OPEN_APPEND, 0);
    assert(append >= 0 && append != file);
    assert(fs_seek(append, 0, SEEK_SET) == 0);
    assert(fs_write(append, "two", 3) == 3);
    assert(fs_close(file) == 0);
    assert(fs_write(append, "!", 1) == 1);
    assert(fs_close(append) == 0);
    assert(memcmp(disk.data[3], disk.data[4], 512) == 0);

    fs.close_path(cwd.index);
    cwd = fs.open_path("/SHELL/");
    assert(cwd.status);
    process.file_handle = (u64)cwd.index << 32;
    file = fs_open("new.txt", RD_ONLY, 0);
    assert(file >= 0);
    char contents[520];
    assert(fs_read(file, contents, sizeof(contents)) == 519);
    for (unsigned i = 0; i < 512; ++i)
        assert(contents[i] == 0);
    assert(memcmp(contents + 512, "onetwo!", 7) == 0);

    int other = fs_open("new.txt", WR_ONLY | FS_OPEN_TRUNCATE, 0);
    assert(other >= 0 && other != file);
    assert(fs_seek(file, 0, SEEK_END) == 0);
    assert(stat("new.txt", &metadata) == 0 && metadata.size == 0);
    assert(fs_write(other, "x", 1) == 1);
    assert(fs_close(other) == 0);
    assert(fs_seek(file, 0, SEEK_SET) == 0 && fs_read(file, contents, sizeof(contents)) == 1);
    assert(contents[0] == 'x');
    assert(fs_close(file) == 0);

    file = fs_open("/readonly", WR_ONLY | create, 0444);
    assert(file >= 0 && fs_close(file) == 0);
    assert(fs_open("/readonly", WR_ONLY, 0) == -FS_OPEN_ERR_ACCESS);
    assert(stat("/readonly", &metadata) == 0 && metadata.mode == 0100444);

    // Fill the first directory cluster and verify growth and persistence.
    char name[] = "file00";
    for (unsigned i = 0; i < 19; ++i) {
        name[4] = '0' + i / 10;
        name[5] = '0' + i % 10;
        file = fs_open(name, WR_ONLY | create, 0666);
        assert(file >= 0 && fs_close(file) == 0);
    }
    fs.close_path(cwd.index);
    assert(stat("/shell/file18", &metadata) == 0 && metadata.size == 0);
    file = fs_open("/shell/new.txt", WR_ONLY, 0);
    assert(file >= 0 && fs_seek(file, 25 * 512, SEEK_SET) == 25 * 512);
    u8 fat_before[512];
    memcpy(fat_before, disk.data[3], sizeof(fat_before));
    assert(fs_write(file, "!", 1) == -1);
    assert(fs_close(file) == 0);
    assert(memcmp(fat_before, disk.data[3], sizeof(fat_before)) == 0);
    assert(stat("/shell/new.txt", &metadata) == 0 && metadata.size == 1);
    assert(memcmp(disk.data[3], disk.data[4], 512) == 0);
    return 0;
}
