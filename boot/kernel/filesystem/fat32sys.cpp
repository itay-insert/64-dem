#include "filesystem/fs.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/timers/rtc.h"
#include "filesystem/fs_flags.h"
#include "filesystem/fat32sys.hpp"
#include "x86-64/scheduler.hpp"

extern Proccess *pid;
extern int current_proccess;
extern BootFileSystem *fat32_fs;


char *fs_join_path(const char *parent, const char *son) {
    if (!parent || !son || parent[0] != '/' || son[0] == '\0')
        return nullptr;

    unsigned parent_length = 0, son_length = 0;
    while (parent_length < 4096 && parent[parent_length]) ++parent_length;
    while (son_length < 4096 && son[son_length]) ++son_length;
    if (parent_length == 4096 || son_length == 4096)
        return nullptr;

    // Normalize only the joining boundary; spaces are significant in FAT names.
    while (parent_length > 1 && parent[parent_length - 1] == '/')
        --parent_length;
    unsigned start = 0;
    while (start < son_length && son[start] == '/') ++start;
    bool separator = parent[parent_length - 1] != '/' && start < son_length;
    unsigned length = parent_length + (separator ? 1 : 0) + son_length - start;
    if (length > 4095)
        return nullptr;

    char *result = new char[length + 1];
    unsigned used = 0;
    for (unsigned i = 0; i < parent_length; ++i) result[used++] = parent[i];
    if (separator) result[used++] = '/';
    for (unsigned i = start; i < son_length; ++i) result[used++] = son[i];
    // A child consisting entirely of slashes denotes the parent directory.
    if (start == son_length && result[used - 1] != '/') {
        delete[] result;
        if (parent_length + 1 > 4095) return nullptr;
        result = new char[parent_length + 2];
        for (unsigned i = 0; i < parent_length; ++i) result[i] = parent[i];
        result[parent_length] = '/';
        used = parent_length + 1;
    }
    result[used] = '\0';
    return result;
}

bool fs_split_path(const char *path, char **parent_out, char **filename_out) {
    if (parent_out) *parent_out = nullptr;
    if (filename_out) *filename_out = nullptr;
    if (!parent_out || !filename_out || parent_out == filename_out || !path)
        return false;

    unsigned length = 0;
    while (length < 4096 && path[length])
        ++length;
    if (length == 4096 || length == 0 || path[length - 1] == '/')
        return false;

    // Each parent component occupies 11 bytes followed by a slash.
    unsigned start = path[0] == '/' ? 1 : 0;
    unsigned separators = 0;
    for (unsigned i = start; i < length; ++i)
        if (path[i] == '/') ++separators;
    if (1 + separators * 12 > 4095)
        return false;
    char *parent = new char[2 + separators * 12];
    unsigned used = 0;
    parent[used++] = '/';

    for (unsigned end = start; end <= length; ++end) {
        if (end != length && path[end] != '/')
            continue;
        unsigned count = end - start;
        // Maximum textual short name is eight characters, dot, three chars.
        if (count == 0 || count > 12) {
            delete[] parent;
            return false;
        }
        char component[13];
        for (unsigned i = 0; i < count; ++i)
            component[i] = path[start + i];
        component[count] = '\0';
        char *encoded = convert_str_to_fat32(component);
        if (!encoded) {
            delete[] parent;
            return false;
        }
        if (end == length) {
            parent[used] = '\0';
            *parent_out = parent;
            *filename_out = encoded;
            return true;
        }
        for (unsigned i = 0; i < 11; ++i)
            parent[used++] = encoded[i];
        parent[used++] = '/';
        delete[] encoded;
        start = end + 1;
    }
    delete[] parent;
    return false;
}


// Resolve paths relative to the directory that holds the running executable.
static bool resolve_path(const char *path, char *parent, char *filename, bool &directory) {
    if (!path || !*path)
        return false;

    unsigned length = 0;
    while (length < 4096 && path[length])
        ++length;
    if (length == 4096)
        return false;

    unsigned used = 1;
    parent[0] = '/';
    if (path[0] != '/' && pid) {
        const char *cwd = fat32_fs->return_str(pid[current_proccess].file_handle >> 32);
        if (cwd) {
            used = 0;
            while (used < 4095 && cwd[used]) {
                parent[used] = cwd[used];
                ++used;
            }
            if (!used || used == 4095)
                return false;
            if (parent[used - 1] != '/')
                parent[used++] = '/';
        }
    }

    directory = true;
    for (unsigned start = 0; start < length;) {
        if (path[start] == '/') {
            ++start;
            directory = true;
            continue;
        }
        unsigned end = start;
        while (end < length && path[end] != '/')
            ++end;
        unsigned size = end - start;
        directory = path[start] == '.' && (size == 1 || (size == 2 && path[start + 1] == '.'));
        if (directory) {
            if (size == 2 && used > 1) {
                parent[used] = '\0';
                if (!fat32_fs->find_open_path(parent).status) {
                    Path_ret result = fat32_fs->open_path(parent);
                    if (!result.status)
                        return false;
                    fat32_fs->close_path(result.index);
                }
                --used;
                while (parent[used - 1] != '/')
                    --used;
            }
        } else {
            if (size > 12 || used + 12 > 4095)
                return false;
            char name[13];
            memcpy(name, path + start, size);
            name[size] = '\0';
            char *encoded = convert_str_to_fat32(name);
            if (!encoded)
                return false;
            memcpy(parent + used, encoded, 11);
            delete[] encoded;
            used += 11;
            parent[used++] = '/';
        }
        start = end;
    }

    filename[0] = '\0';
    if (used > 1) {
        unsigned start = used - 1;
        while (parent[start - 1] != '/')
            --start;
        unsigned size = used - start - 1;
        memcpy(filename, parent + start, size);
        filename[size] = '\0';
        used = start;
    }
    parent[used] = '\0';
    return true;
}

static int reserve_file_id(Proccess &process) {
    for (int i = 0; i < process.file_count; ++i) {
        if (!process.files[i].active)
            return i;
    }
    int fd = process.file_count;
    auto *files = (file_id *)krealloc(process.files, fd * sizeof(file_id),
                                     (fd + 1) * sizeof(file_id));
    if (!files)
        return -1;
    process.files = files;
    ++process.file_count;
    return fd;
}

static file_id *get_file_id(int fd) {
    if (!pid || fd < 0)
        return nullptr;
    Proccess &process = pid[current_proccess];
    if (fd >= process.file_count || !process.files[fd].active)
        return nullptr;
    return &process.files[fd];
}

static int open_entry(u32 index, const char *filename, int flags, unsigned mode) {
    FAT32_DIR entry;
    bool exists = fat32_fs->lookup_entry(index, filename, entry);
    bool create = flags & FS_OPEN_CREATE;
    bool writable = (flags & ACCMODE) != RD_ONLY;
    if (exists) {
        if (create && (flags & FS_OPEN_EXCLUSIVE))
            return -FS_OPEN_ERR_EXISTS;
        if (entry.attributes & 0x10)
            return -FS_OPEN_ERR_DIRECTORY;
        if (writable && (entry.attributes & 0x01))
            return -FS_OPEN_ERR_ACCESS;
    } else if (!create) {
        return -FS_OPEN_ERR_NO_ENTRY;
    }

    Proccess &process = pid[current_proccess];
    int fd = reserve_file_id(process);
    if (fd < 0)
        return -FS_OPEN_ERR_IO;
    if (!exists && !fat32_fs->fat32_make_entry(index, filename,
                                              !(mode & (S_IWUSR | S_IWGRP | S_IWOTH))))
        return -FS_OPEN_ERR_NO_SPACE;

    u64 handle = fat32_fs->open_file_by_index(index, filename);
    if (handle == 0xFFFFFFFF)
        return -FS_OPEN_ERR_IO;
    if (exists && (flags & FS_OPEN_TRUNCATE) && !fat32_fs->truncate_file(handle)) {
        fat32_fs->close_file(handle);
        return -FS_OPEN_ERR_IO;
    }
    process.files[fd] = {handle, 0, flags, true};
    return fd;
}

extern "C" int fs_open(const char *path, int flags, unsigned mode) {
    constexpr int supported = ACCMODE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE |
                              FS_OPEN_TRUNCATE | FS_OPEN_APPEND;
    if (!fat32_fs || !pid || (flags & ~supported) || (flags & ACCMODE) == ACCMODE ||
        ((flags & FS_OPEN_TRUNCATE) && (flags & ACCMODE) == RD_ONLY))
        return -FS_OPEN_ERR_INVALID;

    char parent[4096];
    char filename[12];
    bool directory;
    if (!resolve_path(path, parent, filename, directory))
        return -FS_OPEN_ERR_NO_ENTRY;
    if (!filename[0])
        return -FS_OPEN_ERR_DIRECTORY;

    Path_ret cached = fat32_fs->find_open_path(parent);
    Path_ret path_result = cached.status ? cached : fat32_fs->open_path(parent);
    if (!path_result.status)
        return -FS_OPEN_ERR_NO_ENTRY;

    int fd;
    if (directory) {
        FAT32_DIR entry;
        if (!fat32_fs->lookup_entry(path_result.index, filename, entry))
            fd = -FS_OPEN_ERR_NO_ENTRY;
        else
            fd = (entry.attributes & 0x10) ? -FS_OPEN_ERR_DIRECTORY : -FS_OPEN_ERR_NOT_DIR;
    } else {
        fd = open_entry(path_result.index, filename, flags, mode);
    }
    if (fd < 0 && !cached.status)
        fat32_fs->close_path(path_result.index);
    return fd;
}

extern "C" i64 fs_read(int fd, void *buffer, u64 count) {
    file_id *id = get_file_id(fd);
    if (!id || (id->flags & ACCMODE) == WR_ONLY)
        return -1;
    u64 size = fat32_fs->get_filesize(id->file_handle);
    if (!count || id->file_offset >= size)
        return 0;
    u64 remaining = size - id->file_offset;
    if (count > remaining)
        count = remaining;

    if (!fat32_fs->read_file(id->file_handle, id->file_offset, (u32)count, buffer))
        return -1;
    id->file_offset += count;
    return (i64)count;
}

extern "C" i64 fs_write(int fd, const void *buffer, u64 count) {
    file_id *id = get_file_id(fd);
    if (!id || (id->flags & ACCMODE) == RD_ONLY || count > 0xFFFFFFFFULL)
        return -1;
    if (!count)
        return 0;

    u64 offset = (id->flags & FS_OPEN_APPEND) ? fat32_fs->get_filesize(id->file_handle)
                                              : id->file_offset;
    if (!fat32_fs->write_file(id->file_handle, offset, (u32)count, buffer))
        return -1;
    id->file_offset = offset + count;
    return (i64)count;
}

extern "C" i64 fs_seek(int fd, i64 offset, int whence) {
    file_id *id = get_file_id(fd);
    if (!id)
        return -1;

    i64 base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = id->file_offset; break;
        case SEEK_END: base = fat32_fs->get_filesize(id->file_handle); break;
        default: return -1;
    }
    if (offset < -base || offset > 0xFFFFFFFFLL - base)
        return -1;
    id->file_offset = base + offset;
    return (i64)id->file_offset;
}

extern "C" int fs_close(int fd) {
    file_id *id = get_file_id(fd);
    if (!id)
        return -1;

    fat32_fs->close_file(id->file_handle);
    *id = {};
    return 0;
}

extern u64 former_user_rsp;
extern u64 current_user_rsp;

extern "C" void fs_start(int fd, u64 arg) {
    file_id *id = get_file_id(fd);
    if (!id)
        return;

    former_user_rsp = current_user_rsp;

    StartProccess(id->file_handle, fat32_fs, arg);
}


struct fs_stat {
    uint32_t mode;
    uint32_t reserved;  /* Set to zero. */
    uint64_t size;
    int64_t mtime;      /* Seconds since the Unix epoch. */
};


static bool is_leap_year(unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int64_t fat32_mtime(const FAT32_DIR &entry) {
    unsigned year = 1980 + (entry.last_modify_date >> 9);
    unsigned month = (entry.last_modify_date >> 5) & 0x0F;
    unsigned day = entry.last_modify_date & 0x1F;
    unsigned hour = entry.last_modify_time >> 11;
    unsigned minute = (entry.last_modify_time >> 5) & 0x3F;
    unsigned second = (entry.last_modify_time & 0x1F) * 2;

    unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    month_days[1] += is_leap_year(year);
    if (month == 0 || month > 12 || day == 0 || day > month_days[month - 1] ||
        hour > 23 || minute > 59 || second > 59)
        return 0;

    int64_t days = day - 1;
    for (unsigned y = 1970; y < year; ++y)
        days += 365 + is_leap_year(y);
    for (unsigned m = 1; m < month; ++m)
        days += month_days[m - 1];

    // FAT32 timestamps carry no time zone. Interpret the stored time as UTC.
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

static bool fat32_fill_stat(fs_stat *st, const FAT32_DIR *entry) {
    constexpr u8 read_only = 0x01;
    constexpr u8 volume_label = 0x08;
    constexpr u8 directory = 0x10;
    constexpr uint32_t regular_file_mode = 0100000;
    constexpr uint32_t directory_mode = 0040000;

    if (!st)
        return false;
    *st = {};
    if (!entry || entry->filename[0] == '\0' || (u8)entry->filename[0] == 0xE5 ||
        (entry->attributes & volume_label))
        return false;

    st->mode = S_IRUSR | S_IRGRP | S_IROTH;
    if (!(entry->attributes & read_only))
        st->mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    if (entry->attributes & directory)
        st->mode |= directory_mode | S_IXUSR | S_IXGRP | S_IXOTH;
    else
        st->mode |= regular_file_mode;
    st->size = entry->file_size;
    st->mtime = fat32_mtime(*entry);
    return true;
}

extern "C" int stat(const char *path, struct fs_stat *st) {
    if (!st)
        return 1;
    *st = {};
    if (!fat32_fs)
        return 1;

    char parent[4096];
    char filename[12];
    bool directory;
    if (!resolve_path(path, parent, filename, directory))
        return 1;

    FAT32_DIR entry = {};
    if (!filename[0]) {
        if (!fat32_fs->find_open_path("/").status) {
            Path_ret root = fat32_fs->open_path("/");
            if (!root.status)
                return 1;
            fat32_fs->close_path(root.index);
        }
        entry.filename[0] = '/';
        entry.attributes = 0x10;
    } else {
        Path_ret cached = fat32_fs->find_open_path(parent);
        Path_ret result = cached.status ? cached : fat32_fs->open_path(parent);
        if (!result.status)
            return 1;
        bool found = fat32_fs->lookup_entry(result.index, filename, entry);
        if (!cached.status)
            fat32_fs->close_path(result.index);
        if (!found || (directory && !(entry.attributes & 0x10)))
            return 1;
    }
    return fat32_fill_stat(st, &entry) ? 0 : 1;
}
