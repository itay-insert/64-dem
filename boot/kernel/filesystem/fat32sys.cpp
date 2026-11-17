#include "filesystem/fs.hpp"
#include "drivers/display/vga.h"
#include "x86-64/memory/memory_helpers.h"
#include "x86-64/memory/kmalloc.h"
#include "drivers/timers/rtc.h"
#include "filesystem/fs_flags.h"
#include "filesystem/fat32sys.hpp"
#include "x86-64/scheduler.hpp"

extern Proccess *pid;
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
    if (!parent_out || !filename_out || parent_out == filename_out ||
        !path || path[0] != '/')
        return false;

    unsigned length = 0;
    while (length < 4096 && path[length])
        ++length;
    if (length == 4096 || length < 2 || path[length - 1] == '/')
        return false;

    // Each parent component occupies 11 bytes followed by a slash.
    unsigned separators = 0;
    for (unsigned i = 1; i < length; ++i)
        if (path[i] == '/') ++separators;
    char *parent = new char[2 + separators * 12];
    unsigned used = 0;
    parent[used++] = '/';

    unsigned start = 1;
    for (unsigned end = 1; end <= length; ++end) {
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


bool increase_file_ids(Proccess *proc, u64 file_handle, int flags) {
    if (proc->files == nullptr || proc->file_count == 0) {
        proc->files = new file_id[1];
        proc->files->file_handle = file_handle;
        proc->files->file_offset = 0;
        proc->files->pid = proc->PID;
        proc->files->flags = flags;
        proc->file_count = 1;
        return true;
    }


    u64 old_size = proc->file_count * sizeof(file_id);
    u64 new_size = old_size + sizeof(file_id);
    proc->files = (file_id *)krealloc(proc->files, old_size, new_size);
    if (proc->files == nullptr)
        return false;
    proc->files[proc->file_count].file_handle = file_handle;
    proc->files[proc->file_count].file_offset = 0;
    proc->files[proc->file_count].flags = flags;
    proc->files[proc->file_count].pid = proc->PID;
    proc->file_count++;
    return true;
}



int fs_open(char *path, int flags, unsigned mode) {
    char *filename = new char[13];
    char *parent = new char[13];

    if (!fs_split_path(path, &parent, &filename)) {
        delete[] filename;
        delete[] parent;
        return -1; // invalid id for failure
    }

    if (parent[1] == '\0') {
        u32 path_index = (u32)(pid->file_handle >> 32);
        u64 file_handle = fat32_fs->open_file_by_index(path_index, filename);
`       if (increase_file_ids(pid, file_handle, flags)) 
            return (int)(pid->file_count-1);
        else 
            return -1;
    }

    
    u32 path_index = (u32)(pid->file_handle >> 32);

    char *new_path = fs_join_path()
    // TODO: make return_str function in fat32
}
