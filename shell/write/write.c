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
char *buff = NULL;


extern u64 arg;

u64 size = 0;
u64 index = 0;


char *write_to_buffer(char ch, char *buffer, u64 *size_ptr, u64 *index_ptr, bool max) {
    if (buffer == NULL || *size_ptr == 0) {
        *size_ptr = (rows - 1) * coulmns;
        buffer = (char *)malloc(*size_ptr);
        
        if (buffer[*index_ptr] != ch) 
            buffer[*index_ptr] = ch;
            

        *index_ptr += 1;
        return buffer;
    }

    if (*index_ptr == *size_ptr) {
        if (max)
            return buffer;

        u64 new_size = *size_ptr + ((rows-1) * coulmns);
        buffer = (char *)realloc(buffer, *size_ptr, new_size);
        *size_ptr = new_size;
    }

    if (buffer[*index_ptr] != ch) 
        buffer[*index_ptr] = ch;
    
    *index_ptr += 1;

    return buffer;
}

bool erased = false;

void erase_from_buffer(char *buffer, u64 *size_ptr, u64 *index_ptr) {
    if (buffer == NULL || *size_ptr == 0)
        return;

    erased = true;

    if (*index_ptr == 0) 
        buffer[*index_ptr] = ' ';
    else {
        buffer[*index_ptr-1] = ' ';
    }
}

u64 file_size = 0;

int open_file(void) {
    char *file = (char *)arg;

    int fd = fs_open(file, RD_WR | FS_OPEN_CREATE, 0666);

    if (fd < 0) {
        printf("Write: failed to open file %s\n", file);
        return -1;
    }

    struct fs_stat st;
    if (!fs_stat(file, &st)) {
        printf("write: failed to get stat for file %s\n", file);
        return -1;
    }

    if (st.size > 0) {
        buff = (char *)malloc(st.size);
        fs_read(fd, buff, st.size);
        size = (rows - 1) * coulmns;
        file_buffer = (char *)malloc(size);
        memcpy(file_buffer, buff, st.size);
        file_size = st.size;
        return fd;
    } else {
        size = (rows - 1) * coulmns;
        file_buffer = (char *)malloc(size);
        memset(file_buffer, 0, size);
        return fd;
    }
}


bool printed = false;

void draw_screen(void) {
    char buffer[2] = {0};

    int old_coulmn = 0;
    int old_row = 0;
    CursorGet(&old_row, &old_coulmn); // save old cursor coulmn and row

    cursor_Setpos(0, 0); // set cursor position to the start

    u64 cursor_pos = 0;
    while (cursor_pos < index) {
        buffer[0] = file_buffer[cursor_pos];
        buffer[1] = '\0';
        printf(buffer);
        cursor_pos++;
    }

    if (erased) {
        erased = false;
        if (index > 0)
            index--;
    }

}

int main() {
    clear_screen();

    u64 shellsize = getshellsize();

    rows = (int)(shellsize >> 32);
    coulmns = (int)shellsize;


    size = 0;
    index = 0;
    file_buffer = NULL;


    int fd = open_file();
    if (fd < 0) {
        printf("WRITE.ELF: failed to open file %s\n", (char *)arg);
        return 1;
    }

    cursor_Setpos(0, rows-1);
    const char *cases[] = {
        "('New file') ",
        "('file') "
    };



    bool index = (file_size > 0);
    printf("%s %s exit: Ctrl+X  save: Ctrrl+S");
    cursor_Setpos(0, 0);

    while (1) {
        u8 key = read_key(true, false);
        if (key > 0 && key < 0x7f) {
            printed = false;
            file_buffer = write_to_buffer(key, file_buffer, &size, &index, true);
            int row;
            int coulmn;
            CursorGet(&row, &coulmn);
            if (!((row == (rows-1)) && (coulmn == coulmns)))  // have no idea what i did here
                move_cursor(1);
            
        } else if (key == '\b') {
            printed = false;
            erase_from_buffer(file_buffer, &size, &index);
            int row;
            int coulmn;
            CursorGet(&row, &coulmn);
            if (!((row == 0) && (coulmn == 0)))
                move_cursor(-1);
        }

        if (key == 0x1b)
            return 0;
   
        
        if (!printed) {
            draw_screen();
            printed = true;
        }
    }
    
}