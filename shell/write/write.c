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

u64 *index_cache = NULL;
u64 index_capacity = 0;
u64 index_count = 0;
u64 index_handle = 0;
u64 index_size = 0;

static void increase_cache(u64 index) {
    if (index_cache == NULL || index_count == 0) {
        index_cache = (u64 *)malloc(64);
        index_size = 64;
        index_capacity = 8;
        index_count = 0;
        index_cache[index_count] = index;
        index_count++;
        index_capacity--;
        return;
    }


    if (index_capacity == 0) {
        index_cache = (u64 *)realloc(index_cache, index_size, index_size+64);
        index_size += 64;
        index_capacity = 8;
    }

    index_cache[index_count] = index;
    index_count++;
    index_capacity--;
}



char *write_to_buffer(char ch, char *buffer, u64 *size_ptr, u64 *index_ptr, bool max) {
    if (buffer == NULL || *size_ptr == 0) {
        *size_ptr = (rows - 1) * coulmns;
        buffer = (char *)malloc(*size_ptr);
        
        if (buffer[*index_ptr] != ch) {
            buffer[*index_ptr] = ch;
            diff[*index_ptr] = true;
            increase_cache(*index_ptr);
        }

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

    if (buffer[*index_ptr] != ch) {
        buffer[*index_ptr] = ch;
        diff[*index_ptr] = true;
        if (!diff[*index_ptr-1])
            increase_cache(*index_ptr);
    }
    *index_ptr += 1;

    return buffer;
}


void erase_from_buffer(char *buffer, u64 *size_ptr, u64 *index_ptr) {
    if (buffer == NULL || *size_ptr == 0)
        return;

    if (buffer[*index_ptr] != ' ') {
        buffer[*index_ptr] = ' ';
        diff[*index_ptr] = true;
        if ((*index_ptr == 0) || ((*index_ptr > 0) && !diff[*index_ptr-1])) 
            increase_cache(*index_ptr);
    }

    if (*index_ptr > 0)
        *index_ptr -= 1;
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


u64 get_Drawindex(void) {
    if (index_handle < index_count) {
        u64 place = index_cache[index_handle];
        index_handle++;
        if (index_handle == index_count) {
            free(index_cache);
            index_cache = NULL;
            index_capacity = 0;
            index_size = 0;
            index_count = 0;
            index_handle = 0;
        }

        return place;
    }

    return 0xFFFFFFFF; // no index found
}


static inline char *make_str(u64 drawindex) {
    int count = 0;
    u64 countindex = drawindex;

    while (diff[countindex]) {
        diff[countindex] = false;
        countindex++;
        count++;
    }

    if (count != 0) {
        char *str = (char *)malloc(count+1);
        for (int i = 0; i < count; i++) 
            str[i] = file_buffer[drawindex+i];

        str[count] = '\0'; // null terminator
        return str;
    } else
        return NULL; // for failure
}


void draw_screen(void) {
    char buffer[2] = {0};

    int old_coulmn = 0;
    int old_row = 0;
    CursorGet(&old_row, &old_coulmn); // save old cursor coulmn and row

    cursor_Setpos(0, 0); // set cursor position to the start

    while (1) {
        u64 draw_index = get_Drawindex();
        if (draw_index == 0xFFFFFFFF)
            break;
            
        int coulmn;
        int row;
        Get(&row, &coulmn, draw_index); // convert draw_index to cursor row and coulmn
        cursor_Setpos(coulmn, row);

        char *str = make_str(draw_index);
        if (str != NULL) {
            printf(str);
            free(str);
        }
    }

    cursor_Setpos(old_coulmn, old_row);

}

int main() {
    clear_screen();

    u64 shellsize = getshellsize();

    rows = (int)(shellsize >> 32);
    coulmns = (int)shellsize;

    buff = (u8 *)malloc((rows-1)*coulmns);
    memset(buff, 0, (rows-1)*coulmns);

    diff = (bool *)malloc((rows-1)*coulmns);

    memset(diff, 0, (rows-1)*coulmns);

    size = 0;
    index = 0;
    file_buffer = NULL;

    cursor_Setpos(0, 0);


    while (1) {
        u8 key = read_key(true, false);
        if (key >= ' ' && key < 0x7f) {
            file_buffer = write_to_buffer(key, file_buffer, &size, &index, true);
            int row;
            int coulmn;
            CursorGet(&row, &coulmn);
            if (!((row == (rows-1)) && (coulmn == coulmns)))  // have no idea what i did here
                move_cursor(1);
            
        } else if (key == '\b') {
            erase_from_buffer(file_buffer, &size, &index);
            int row;
            int coulmn;
            CursorGet(&row, &coulmn);
            if (!((row == 0) && (coulmn == 0)))
                move_cursor(-1);
        }

        if (key == 0x1b)
            return 0;
   
        draw_screen();
    }
    
}