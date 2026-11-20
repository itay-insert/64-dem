#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


int rows = 0;
int coulmns = 0;

u8 *buff = NULL;
bool *diff = NULL;

extern u64 arg;

int main() {
    u64 shellsize = getshellsize(void);

    rows = (int)(shellsize >> 32);
    coulmns = (int)shellsize;

    buff = (u8 *)malloc(rows*coulmns);
    memset(buff, 0, rows*coulmns);

    diff = (bool *)malloc(rows*coulmns);

    memset(diff, 0, rows*coulmns);

    char *file = (char *)arg;

    int fd = fs_open(file, RD_WR, )
}