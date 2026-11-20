#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#include "os_64dem.h"

extern long Rows;
extern long Columns;

int get_size(char *str) {
    int count = 0;
    while (*str != '\0') {
        str++;
        count++;
    }
    return count;
}



void *memcpy(const void *dest, const void *src, unsigned long size) {
    unsigned char *destination = (unsigned char *)dest;
    unsigned char *source = (unsigned char *)src;

    for (unsigned long i = 0; i < size; i++)
        destination[i] = source[i];

    return destination;
}


int memcmp(const void *ptr0, const void *ptr1, unsigned long size) {
    unsigned char *first = (unsigned char *)ptr0;
    unsigned char *second = (unsigned char *)ptr1;


    for (unsigned long i = 0; i < size; i++)
        if (first[i] != second[i])
            return 1;

    return 0;
}



int str_cmp(const char *ptr0, const char *ptr1) {
    while (*ptr0 != '\0' || *ptr1 != '\0') {
        if (*ptr0 != *ptr1)
            return (unsigned char)(*ptr0 - *ptr1);

        ptr0++;
        ptr1++;
    }

    return (unsigned char)(*ptr0 - *ptr1);
}


void *memset(const void *ptr, int value, unsigned long size) {
    unsigned char val = (unsigned char)value;
    unsigned char *buff = (unsigned char *)ptr;

    for (unsigned long i = 0; i < size; i++)
        buff[i] = val;

    return buff;
}


#ifdef VIM_64DEM
#include "key_codes.h"

static u8 pending_key;
static unsigned char key_sequence[3];
static unsigned sequence_offset = sizeof(key_sequence);

static int dem_read_keys(unsigned char *buff, int maxlen) {
    int count = 0;
    while (count < maxlen) {
        if (sequence_offset < sizeof(key_sequence)) {
            buff[count++] = key_sequence[sequence_offset++];
            continue;
        }

        u8 key = pending_key;
        pending_key = 0;
        if (!key)
            key = read_key(true, count == 0);
        if (!key)
            break;
        if (key >= DEM_KEY_UP && key <= DEM_KEY_LEFT) {
            key_sequence[0] = 0x1b;
            key_sequence[1] = '[';
            key_sequence[2] = "ABCD"[key - DEM_KEY_UP];
            sequence_offset = 0;
        } else {
            if (key == '\n')
                key = '\r';
            buff[count++] = key;
        }
    }
    return count;
}
#else
int mch_inchar(unsigned char *buff, int maxlen, long wtime, int tb_change_cnt) {
    if (maxlen > 256)
        return 0;

    int count = 0;
    char key = 0;

    for (int i = 0; i < maxlen; i++) {
        if (wtime > 0) {
            for (int j = 0; j < wtime; j++) {
                count = mch_inchar(buff, maxlen, 0, tb_change_cnt);
                if (count > 0)
                    return count;
                sleep_ms(1);
            }
            return count;
        } else if (wtime == -1) {
            while (key == 0x00) {
                key = read_key(true, false);
            }

            buff[i] = key;
            wtime = 0;
        } else {
            key = read_key(true, false);
            if (key == 0)
                break;
            buff[i] = key;
        }

        count++;
    }

    return count;
}
#endif


void mch_write(unsigned char *s, int len) {
    stdout_write((char *)s, len);
}


int mch_get_shellsize(void) {
    u64 shell_size = getshellsize();
    if (!(u32)shell_size || !(shell_size >> 32))
        return 0;
    Columns = (u32)shell_size;
    Rows = shell_size >> 32;
    return 1;
}

#ifdef VIM_64DEM
int dem_keyboard_ready(void) {
    if (sequence_offset < sizeof(key_sequence))
        return 1;
    if (!pending_key)
        pending_key = read_key(true, false);
    return pending_key != 0;
}
#endif

// Vim's mch_open() expands to open() on this platform.
    int
open(const char *path, int flags, ...)
{
    int supported = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND;
#ifdef O_NOFOLLOW
    // FAT32 has no symbolic links.
    supported |= O_NOFOLLOW;
#endif
    if (flags & ~supported)
    {
        errno = ENOTSUP;
        return -1;
    }

    int access;
    switch (flags & O_ACCMODE)
    {
        case O_RDONLY: access = RD_ONLY; break;
        case O_WRONLY: access = WR_ONLY; break;
        case O_RDWR: access = RD_WR; break;
        default:
            errno = EINVAL;
            return -1;
    }

    unsigned mode = 0;
    if (flags & O_CREAT)
    {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, unsigned int);
        va_end(args);
        access |= FS_OPEN_CREATE;
        if (flags & O_EXCL)
            access |= FS_OPEN_EXCLUSIVE;
    }
    if (flags & O_TRUNC)
        access |= FS_OPEN_TRUNCATE;
    if (flags & O_APPEND)
        access |= FS_OPEN_APPEND;

    int fd = fs_open(path, access, mode);
    if (fd >= 0)
#ifdef VIM_64DEM
        return fd + 3;
#else
        return fd;
#endif

    switch (-fd)
    {
        case FS_OPEN_ERR_NO_ENTRY: errno = ENOENT; break;
        case FS_OPEN_ERR_ACCESS: errno = EACCES; break;
        case FS_OPEN_ERR_EXISTS: errno = EEXIST; break;
        case FS_OPEN_ERR_NOT_DIR: errno = ENOTDIR; break;
        case FS_OPEN_ERR_DIRECTORY: errno = EISDIR; break;
        case FS_OPEN_ERR_INVALID: errno = EINVAL; break;
        case FS_OPEN_ERR_NO_SPACE: errno = ENOSPC; break;
        default: errno = EIO; break;
    }
    return -1;
}

ssize_t read(int fd, void *buffer, size_t count) {
    if (fd < 0 || count > UINT32_MAX) {
        errno = fd < 0 ? EBADF : EINVAL;
        return -1;
    }

#ifdef VIM_64DEM
    if (fd == 0)
        return dem_read_keys(buffer, count > INT32_MAX ? INT32_MAX : (int)count);
    fd -= 3;
#endif
    i64 result = fs_read(fd, buffer, count);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return result;
}

ssize_t write(int fd, const void *buffer, size_t count) {
    if (fd < 0 || count > UINT32_MAX) {
        errno = fd < 0 ? EBADF : EINVAL;
        return -1;
    }

#ifdef VIM_64DEM
    if (fd == 1 || fd == 2) {
        if (count > INT32_MAX) {
            errno = EINVAL;
            return -1;
        }
        mch_write((unsigned char *)buffer, (int)count);
        return count;
    }
    fd -= 3;
#endif
    i64 result = fs_write(fd, buffer, count);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return result;
}

off_t lseek(int fd, off_t offset, int whence) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    if ((whence == SEEK_SET && offset < 0)
            || (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)) {
        errno = EINVAL;
        return -1;
    }

#ifdef VIM_64DEM
    fd -= 3;
#endif
    i64 result = fs_seek(fd, offset, whence);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return result;
}

int close(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

#ifdef VIM_64DEM
    fd -= 3;
#endif
    int result = fs_close(fd);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return result;
}

    int
stat(const char *path, struct stat *st)
{
    struct fs_stat entry;

    if (fs_stat(path, &entry) != 0)
    {
        errno = ENOENT;
        return -1;
    }

    *st = (struct stat){0};
    st->st_mode = entry.mode;
    st->st_size = entry.size;
    st->st_mtime = entry.mtime;
    return 0;
}
