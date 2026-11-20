#ifndef SYS_H
#define SYS_H
#include "uint_definitions.h"
#include <stdbool.h>
#include "fs_flags.h"

typedef struct {
    u8 year, month, day;
    u8 hour, min, sec;
} rtc_data;

struct fs_stat {
    u32 mode;
    u32 reserved;
    u64 size;
    i64 mtime;
};

void kprintf(const char *str);
u8 read_key(bool ascii, bool wait);
void *malloc(u64 Size);
void free(void *alloc);
void *realloc(void *alloc, u64 old_size, u64 new_size);
void sleep_ms(u64 ms);
rtc_data get_dateAndTime(void);
void print_mem(void);
void acpi(bool shutdown);
void ls(char *path);
void backspace(void);

int fs_open(const char *path, int flags, unsigned mode);
i64 fs_read(int fd, void *buffer, u64 count);
i64 fs_write(int fd, const void *buffer, u64 count);
i64 fs_seek(int fd, i64 offset, int whence);
int fs_close(int fd);
int fs_stat(const char *path, struct fs_stat *st);
void fs_start(int fd);
void print_drives(void);
void beep(unsigned int hz, u64 ms);
void set_begin(void);
u64 read_freq();
void stdout_write(char *buff, int len);
u64 getshellsize(void);

#endif
