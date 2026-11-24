#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


int rows = 0;
int coulmns = 0;


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
u64 file_size = 0;
u64 file_top = 0;


void erase_from_buffer(char *buffer, u64 *size_ptr, u64 *index_ptr) {
    if (buffer == NULL || *size_ptr == 0)
        return;

    erased = true;

    if (*index_ptr == 0) 
        buffer[*index_ptr] = ' ';
    else {
        buffer[*index_ptr-1] = ' ';
    }

    if (*index_ptr < file_top) {
        for (u64 i = (*index_ptr-1); i < (file_top-1); i++) 
            buffer[i] = buffer[i+1];
        
        file_top--;
    }
}


int open_file(void) {
    char *file = (char *)arg;

    int fd = fs_open(file, RD_WR | FS_OPEN_CREATE, 0666);

    if (fd < 0) {
        printf("Write: failed to open file %s\n", file);
        return -1;
    }

    struct fs_stat st;
    if (fs_stat(file, &st) != 0) {
        printf("write: failed to get stat for file %s\n", file);
        fs_close(fd);
        return -1;
    }

    if (st.size > 0) {
        buff = (char *)malloc(st.size);
        fs_read(fd, buff, st.size);
        size = (rows - 1) * coulmns;
        file_buffer = (char *)malloc(size);
        memcpy(file_buffer, buff, st.size);
        index = st.size;
        file_size = st.size;
        file_top = index;
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
    while (cursor_pos < file_top) {
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

#define UP 0x48
#define LEFT 0x4B
#define RIGHT 0x4D
#define DOWN 0x50



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
        "('New file')",
        "('file')"
    };



    bool case_index = (file_size > 0);
    printf("%s %s exit: Ctrl+X  save: Ctrl+S", cases[(u8)case_index], (char *)arg);
    if (file_size > 0) {
        cursor_Setpos(0, 0);
        draw_screen();
    }

    cursor_Setpos((index % coulmns), (index / coulmns));

    while (1) {
        u8 scancode = read_key(false, false);
        u8 key = read_key(true, false);


        // arrow key cursor movement
        switch (scancode) {
            case LEFT: {
                if (index > 0) {
                    index--;
                    int row;
                    int coulmn;
                    CursorGet(&row, &coulmn); // get cursor row and coulmn
                    if (!((row == 0) && (coulmn == 0)))
                        move_cursor(-1); // move cursor too and make sure it doesn't go past the left border
                }

                goto exit;
            }


            case RIGHT: {
                if (index < size) {
                    index++;
                    int row;
                    int coulmn;
                    CursorGet(&row, &coulmn); // get cursor row and coulmn
                    if (!((row == (rows-1)) && (coulmn == coulmns)))  // move cursor too but make sure it doesn't go past the screen
                        move_cursor(1);
                }
                
                goto exit;
            }


            case UP: {
                int row;
                int coulmn;
                CursorGet(&row, &coulmn); // get current cursor row and coulmn

                if (row > 0) {
                    row--;
                    cursor_Setpos(coulmn, row);
                }

                goto exit;
            }


            case DOWN: {
                int row;
                int coulmn;
                CursorGet(&row, &coulmn); // get current cursor row and coulmn
                
                if (row < (rows-2)) {
                    row++;
                    cursor_Setpos(coulmn, row);
                }

                goto exit;
            }


            default: {
                break;
            }
        }

        if (key > 0 && key < 0x7f) {
            printed = false;
            file_buffer = write_to_buffer(key, file_buffer, &size, &index, true);
            u64 cursor_pos = Cursor_Getpos();
            if (cursor_pos >= file_top)
                file_top = cursor_pos; // distinguish current index/cursor_pos from top char 
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
   
        
        exit:

        if (!printed) {
            draw_screen();
            printed = true;
        }
    }
    
}
