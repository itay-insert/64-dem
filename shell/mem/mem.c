#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"

int main() {
    char *str = (char *)malloc(4);
    str[0] = 'H';
    str[1] = 'I';
    str[2] = '\n';
    str[3] = '\0';
    kprintf(str);
    printf("Allocation address: 0x%lx\n", (u64)str);
    int *nums = (int *)malloc(2);
    *nums = 1;
    nums[1] = 2;
    printf("A: %d  B: %d\n", *nums, nums[1]);
    printf("Second allocation: 0x%lx\n", (u64)nums);
    free(nums);
    free(str);

    int a = 5;
    int *a_ptr = &a;
    printf("A: %d  address of A: 0x%lx\n", *a_ptr, (u64)a_ptr);
    sleep_ms(5000);
}