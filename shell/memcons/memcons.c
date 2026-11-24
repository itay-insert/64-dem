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
            return index;

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
    for (int i = index; i < (index+coulmns); i++) {
        row_str[i] = memcons[i];
        if (row_str[i] == '\n') {
            row_str[i+1] = '\0';
            return row_str;
        }
    }

    row_str[index+coulmns] = '\0';
    return row_str;
}


static inline void display_row(int row) {
    if (row >= rows_count)
        return;

    int index = row_lines[row];
    printf(make_str())
}

static void display_rows(void) {
    for (int i = row_start; i < row_end; i++) {
        if (i >= rows_count)
            return;

        display_row(i);
    }
}

int main() {
    row_lines = (row_desc *)malloc(64000); // allocate 64kb for row_newlines
    memset(row_lines, 0, 64000);

    get(&rows, &coulmns, getshellsize()); // get amount of rows and coulmns

    row_end = rows;
    row_str = (char *)malloc(coulmns+1); // allocate for row_str

    memcons_size = get_memcons_size();
    memcons = (char *)malloc(memcons_size);

    get_memcons(memcons);

    count_rows();

    
}