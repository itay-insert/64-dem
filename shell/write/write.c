#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


int rows = 0;
int coulmns = 0;

u8 *buff = NULL;
bool *diff = NULL;

u8 *file_buffer = NULL;

extern u64 arg;

u64 size = 0;
u64 index = 0;

void write_to_buffer(char ch, char *buffer, u64 *size_ptr, u64 *index_ptr, bool max) {
    if (buffer == NULL || *size_ptr == 0) {
        *size_ptr = (rows - 1) * coulmns;
        buffer = (char *)malloc(*size_ptr);
        buffer[*index_ptr] = ch;
        *index_ptr++;
        return;
    }

    if (*index_ptr == *size_ptr) {
        if (max)
            return;

        u64 new_size = *size_ptr + ((rows-1) * coulmns);
        buffer = (char *)realloc(buffer, *size_ptr, new_size);
        *size_ptr = new_size;
    }

    buffer[*index_ptr] = ch;
    *index_ptr++;
}



int open_file(void) {
    char *file = (char *)arg;

    int fd = fs_open(file, RD_WR | FS_OPEN_CREATE, 0666);

    if (fd < 0) {
        printf("Write: failed to open file %s\n", file);
        return 1;
    }

    struct fs_stat st;
    if (!fs_stat(file, &st)) {
        printf("write: failed to get stat for file %s\n", file);
        return 1;
    }
}


int main() {
    u64 shellsize = getshellsize(void);

    rows = (int)(shellsize >> 32);
    coulmns = (int)shellsize;

    buff = (u8 *)malloc((rows-1)*coulmns);
    memset(buff, 0, (rows-1)*coulmns);

    diff = (bool *)malloc((rows-1)*coulmns);

    memset(diff, 0, rows*coulmns);

    size = 0;
    index = 0;
    file_buffer = NULL;

    u8 key = read_key(true, false);
    if (key >= ' ' && key < 0x7f) {
       
    } else if (key == '\b') {
       
    }

    if (key == 0x1b)
        return 0;
   
    
}