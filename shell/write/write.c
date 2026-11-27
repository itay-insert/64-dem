#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


int rows = 0;
int coulmns = 0;

int row_start = 0;
int row_end = 0;


char *buff = NULL;

char *row_str = NULL;

extern u64 arg;

u64 size = 0;
u64 pos = 0;

u64 file_top = 0;

u64 max_size = 0;
bool max = true;



typedef struct {
    bool newline;
    int index;
} row_desc;

row_desc *row_lines = NULL;


bool erased = false;
u64 file_size = 0;

u64 rows_count = 0;

static inline int make_row(int index) {
    for (int i = index; i < (coulmns+index); i++) {
        if (i >= file_size)
            return file_size; // captured an infinite loop here

        if (buff[i] == '\n') {
            row_lines[rows_count].index = index;
            row_lines[rows_count].newline = true;
            index = i + 1;
            rows_count++;
            return index;
        }
    }

    row_lines[rows_count].index = index;
    row_lines[rows_count].newline = false;
    index += coulmns;
    rows_count++;
    return index;
}


static inline void count_rows(void) {
    int index = 0;
    while (index < (file_size-1)) 
        index = make_row(index);
}


static char *make_str(int index) {
    int count = 0;
    for (int i = index; i < (index+coulmns); i++) {
        if (i >= max_size)
            continue;

        row_str[count] = buff[i];
        if (row_str[count] == '\n') {
            row_str[count+1] = '\0';
            return row_str;
        }
        count++;
    }

    row_str[coulmns] = '\0';
    return row_str;
}


static inline void display_row(int row) {
    if (row >= (rows_count-1))
        return;

    int index = row_lines[row].index;
    printf(make_str(index));
}

static void display_rows(void) {
    cursor_Setpos(0, 0); // start to print at 0, 0
    int current_row = 0;
    for (int i = row_start; i < row_end; i++) {
        if (i >= (rows_count-1))
            return;

        clear_row(current_row);
        display_row(i);
        current_row++;
    }

    cursor_Setpos(pos % coulmns, pos / coulmns); // restore cursor coordinates
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
        size = (rows - 1) * coulmns;
        buff = (char *)malloc(st.size+size);
        fs_read(fd, buff, st.size);
        if (st.size < size)
            pos = st.size;
        else 
            pos = 0;

        file_size = st.size;
        file_top = st.size;
        max_size = size + file_size;
        count_rows();
        return fd;
    } else {
        pos = 0;
        size = (rows - 1) * coulmns;
        buff = (char *)malloc(size);
        memset(buff, 0, size);
        max_size = size + file_size;
        return fd;
    }
}


bool printed = false;

static void write_char(char ch) {
    if (pos < max_size && pos >= 0) {
        buff[pos] = ch;
        if (file_top == pos)
            file_top++;
    }

    if (pos <= ((rows-1)*coulmns)) {
        pos++;
        MoveCursor(1, 7);
    }
}


static void erase_char(void) {
    if (pos > 0) {
        buff[pos-1] = ' ';
        if (file_top == pos)
            file_top--;
    }


    if (pos > 0) {
        pos--;
        MoveCursor(-1, 7);
    }
}



#define UP 0x48
#define LEFT 0x4B
#define RIGHT 0x4D
#define DOWN 0x50



int main() {
    clear_screen();

    u64 shellsize = getshellsize();

    row_str = (char *)malloc(coulmns+1); // allocate for row_str

    rows = (int)(shellsize >> 32);
    coulmns = (int)shellsize;

    row_start = 0;
    row_end = (rows-1);

    size = 0;
    pos = 0;
  
    row_lines = (row_desc *)malloc(rows*sizeof(row_lines));


    int fd = open_file();
    if (fd < 0) {
        printf("WRITE.ELF: failed to open file %s\n", (char *)arg);
        return 1;
    }

    cursor_Setpos(0, rows-2);
    const char *cases[] = {
        "('New file')",
        "('file')"
    };



    bool case_index = (file_size > 0);
    printf("%s %s exit: Ctrl+X  save: Ctrl+S", cases[(u8)case_index], (char *)arg);


    cursor_Setpos((pos % coulmns), (pos / coulmns));

    while (1) {
        u8 scancode = read_key(false, false);
        u8 key = read_key(true, false);


        // arrow key cursor movement
        switch (scancode) {
            case LEFT: {
                   // move cursor left if above first coulmn
                if (pos > 0) {
                    MoveCursor(-1, 7);
                    pos--;
                } else {
                    if (row_start > 0) {
                        row_start--;
                        row_end--;
                        printed = false;
                    }
                }

                goto exit;
            }


            case RIGHT: {
                
                if (pos < ((rows*coulmns) - 1)) {
                    MoveCursor(1, 7);
                    pos++;
                } else {
                    if (row_end < rows_count) {
                        row_start++;
                        row_end++;
                        printed = false;
                    }
                }
                
                goto exit;
            }


            case UP: {
                
                if ((pos / coulmns) > 0) {
                    MoveCursor(-coulmns, 7); // if row larger than 0, move cursor row back by one row
                    pos -= coulmns;
                } else {
                    if (row_start > 0) {
                        row_start--;
                        row_end--;
                        printed = false;
                    }
                }

                goto exit;
            }


            case DOWN: {
                if ((pos / coulmns) < (rows-1)) { // if row smaller than last row, move cursor row forwards by one row
                    MoveCursor(coulmns, 7);
                    pos += coulmns;
                } else {
                    if (row_end < rows_count) {
                        row_end++;
                        row_start++;
                        printed = false;
                    }
                }


                goto exit;
            }


            default: {
                break;
            }
        }

        if (key > 0 && key < 0x7f && key != '\b') {
            printed = false;
            write_char(key);
            
        } else if (key == '\b') {
            printed = false;
            erase_char();
        }

        if (key == 0x1b) {

            if (buff != NULL)
                free(buff);


            if (row_str)
                free(row_str);


            return 0;
        }
   
        
        exit:

        if (!printed) {
            if (file_size > 0)
                display_rows();
            printed = true;
        }
    }
    
}
