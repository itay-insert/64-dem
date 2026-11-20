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

extern void StartProccess(u64 file_handle, BootFileSystem *fat32);

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


bool increase_file_ids(Proccess *proc, u64 file_handle, int flags) {
    if (proc->files == nullptr || proc->file_count == 0) {
        proc->files = new file_id[1];
        proc->files->file_handle = file_handle;
        proc->files->file_offset = 0;
        proc->files->pid = proc->PID;
        proc->files->flags = flags;
        proc->files->file_size = fat32_fs->get_filesize(file_handle);
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
    proc->files[proc->file_count].file_size = fat32_fs->get_filesize(file_handle);
    proc->file_count++;
    return true;
}

void destroy_id(Proccess *proc, int fd) {
    if (proc->files == nullptr || proc->file_count == 0)
        return;

    proc->files[fd] = {0};
    for (int i = fd; i < proc->file_count; i++) 
        proc->files[i] = proc->files[i+1];

    u64 old_size = proc->file_count * sizeof(file_id);
    u64 new_size = old_size - sizeof(file_id);

    if (new_size == 0) 
        kfree(proc->files);
    else 
        proc->files = (file_id *)krealloc(proc->files, old_size, new_size);

    proc->file_count--;
}



extern "C" int fs_open(char *path, int flags, unsigned mode) {
    char *filename = new char[13];
    char *parent = new char[13];

    if (!fs_split_path(path, &parent, &filename)) {
        delete[] filename;
        delete[] parent;
        return -1; // invalid id for failure
    }

    if (parent[1] == '\0') {
        u32 path_index = (u32)(pid[current_proccess].file_handle >> 32);
        u64 file_handle = fat32_fs->open_file_by_index(path_index, filename);
        if (increase_file_ids(&pid[current_proccess], file_handle, flags)) 
            return (int)(pid[current_proccess].file_count-1);
        else 
            return -1;
    }

    
    u32 path_index = (u32)(pid[current_proccess].file_handle >> 32);
    char *path_str = fat32_fs->return_str(path_index);

    char *new_path = fs_join_path(path_str, parent);

    u64 file_handle = fat32_fs->open_file_by_path(new_path, filename);
    if (increase_file_ids(&pid[current_proccess], file_handle, flags))
        return (int)(pid[current_proccess].file_count-1);
    else
        return -1;
}



extern "C" i64 fs_read(int fd, void *buffer, u64 count) {
    file_id *id = &pid[current_proccess].files[fd];

    if (count == 0 || id->file_offset >= id->file_size)
        return 0;

    u64 remaining = id->file_size - id->file_offset;
    if (count > remaining)
        count = remaining;

    fat32_fs->read_file(id->file_handle, id->file_offset, (u32)count, buffer);

    id->file_offset += count;
    return (i64)count;
}


extern "C" i64 fs_write(int fd, const void *buffer, u64 count) {
    file_id *id = &pid[current_proccess].files[fd];

    if (count == 0)
        return (i64)count;

    fat32_fs->write_file(id->file_handle, id->file_offset, (u32)count, buffer);

    id->file_offset += count;
    if (id->file_offset > id->file_size)
        id->file_size = id->file_offset;
    return (i64)count;
}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2


extern "C" i64 fs_seek(int fd, i64 offset, int whence) {
    file_id *id = &pid[current_proccess].files[fd];

    const char *str = "FS_SEEK: file_handle: 0x%lx offset too big!\n";

    switch (whence) {
        case SEEK_SET: {
            if (offset >= id->file_size) {
                printf(str, id->file_handle);
                return (i64)id->file_offset;
            }

            id->file_offset = (u64)offset;
            return (i64)id->file_offset;
        }


        case SEEK_CUR: {
            if ((id->file_offset + offset) >= id->file_size) {
                printf(str, id->file_handle);
                return (i64)id->file_offset;
            }

            id->file_offset += (u64)offset;
            return (i64)id->file_offset;
        }


        case SEEK_END: {
            if ((id->file_size - (u64)(-offset)) >= id->file_size) {
                printf(str, id->file_handle);
                return (i64)id->file_offset;
            }

            id->file_offset = (id->file_size - (u64)(-offset));
            return (i64)id->file_offset;
        }


        default: {
            printf("FS_SEEK: unknown whence\n");
            return (i64)id->file_offset;
        }
    }
    
}





extern "C" int fs_close(int fd) {
    file_id *id = &pid[current_proccess].files[fd];


    fat32_fs->close_file(id->file_handle);
    destroy_id(&pid[current_proccess], fd);
    return 0;
}

extern u64 former_user_rsp;
extern u64 current_user_rsp;

extern "C" void fs_start(int fd) {
    file_id *id = &pid[current_proccess].files[fd];

    former_user_rsp = current_user_rsp;

    StartProccess(id->file_handle, fat32_fs);
}


