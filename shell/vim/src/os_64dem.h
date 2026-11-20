
#ifndef OS_64DEM_H
#define OS_64DEM_H

#include <sys/stat.h>

#include "uint_definitions.h"
#include "sys.h"

int get_size(char *str);

void *memcpy(const void *dest, const void *src, unsigned long size);


int memcmp(const void *ptr0, const void *ptr1, unsigned long size);

int str_cmp(const char *ptr0, const char *ptr1);


void *memset(const void *ptr, int value, unsigned long size);


int mch_inchar(unsigned char *buff, int maxlen, long wtime, int tb_change_cnt);


void mch_write(unsigned char *s, int len);

int mch_get_shellsize(void);

int stat(const char *path, struct stat *st);

#endif
