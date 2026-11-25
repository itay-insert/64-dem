#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"

typedef struct {
    bool newline;
    int index;
} row_desc;

row_desc *row_lines = NULL;

char *memcons = NULL;
u64 memcons_size = 0;
u64 rows_count = 0;

int rows;
int coulmns;

static inline int make_row(int index) {
    for (int i = index; i < (coulmns+index); i++) {
        if (i >= memcons_size)
            return memcons_size; // captured an infinite loop here

        if (memcons[i] == '\n') {
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
    while (index < (memcons_size-1)) 
        index = make_row(index);
}


char *row_str = NULL;
int row_start = 0;
int row_end = 0;


static char *make_str(int index) {
    int count = 0;
    for (int i = index; i < (index+coulmns); i++) {
        row_str[count] = memcons[i];
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

int pos = 0;
// moved the declaration up for compatibillity with display_rows()

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


#define UP 0x48
#define LEFT 0x4B
#define RIGHT 0x4D
#define DOWN 0x50

#define ESC 0x01

bool printed = false;


static int viewer(void) {
    u8 scancode = read_key(false, false); // read current key press
    u8 key = read_key(true, false);


    if (key == 0x1b)
        return 1;

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

            break;
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

            break; // advance row_end and row_start if appropriate and if pos is at the right place
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

            break;
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

            break;
        }


        default: {
            break;
        }
    }


    if (!printed) {
        display_rows();
        printed = true;
    }

    return 0;
}



int main() {
    clear_screen();

    row_lines = (row_desc *)malloc(64000); // allocate 64kb for row_newlines
    memset(row_lines, 0, 64000);

    Get(&rows, &coulmns, getshellsize()); // get amount of rows and coulmns

    row_end = rows;
    row_str = (char *)malloc(coulmns+1); // allocate for row_str

    memcons_size = get_memcons_size();
    memcons = (char *)malloc(memcons_size);

    get_memcons(memcons);

    count_rows();
    row_start = 0;
    row_end = ((rows - 1) > (rows_count - 1)) ? (rows_count - 1) : (rows - 1); 
 
    cursor_Setpos(0, 0);

    while (1) {
        if (viewer() == 1) {
            free(row_lines);
            free(memcons);
            free(row_str);
            return 0; // exit if aborted
        }
    }

}