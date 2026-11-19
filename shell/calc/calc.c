#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"

static inline int make_long(char *str, long *num, bool neg) {
    *num = 0;
    int count = 0;
    u8 buff[7] = {'-', '+', '\0', '*', '=', ')', ']'};
    while (1) {
        for (int i = 0; i < 7; i++)
            if (*str == buff[i])
                goto exit;
    
        unsigned int result = ascii_to_digit(*str);
        if (result == 0xFFFFFFFF)
            return count; // failure

       *num = (*num * 10) + (long)result;
        str++;
        count++;
    }

    exit:

    if (neg && *num != 0)
        *num = -*num;

    return count;
}


#define ADD 0
#define SUB 1
#define MUL 2
#define div 3
#define modulo 4
#define shr 6
#define shl 5

static int calc(char *buffer, long *sum) {
    int index = 0;
    long num = 0;
    bool neg = false;
    if (buffer[0] == '-') {
        neg = true;
        index++;
    }

    index += make_long(&buffer[index], &num, neg);
    neg = false;

    const char *ops = "+-*/%:";
    int op = 0;
    while (buffer[index] != '=' && buffer[index] != '\0') {
        if (buffer[index] == ' ' || buffer[index] == ')' || buffer[index] == ']') {
            index++;
            continue;
        }

    
        if (buffer[index] == '>' && buffer[index+1] == '>') {
            op = shr;
            index += 2;
            goto skip;
        } else if (buffer[index] == '<' && buffer[index+1] == '<') {
            op = shl;
            index += 2;
            goto skip;
        }


        for (int i = ADD; i < 5; i++) {
            if (buffer[index] == ops[i]) {
                op = i;
                index++;
                break;
            }

        }

        skip:

        long in = 0;
       

        if (buffer[index] == '(' || buffer[index] == '[') {
            index++;
            index += calc(&buffer[index], &in);
        } else {
            if (buffer[index] == '-') {
                index++;
                neg = true;
            }


            index += make_long(&buffer[index], &in, neg);
            neg = false;
        }
        


        switch (op) {
            case ADD: {
                num += in;
                break;
            }

            case SUB: {
                num -= in;
                break;
            }

            case MUL: {
                num *= in;
                break;
            }

            case div: {
                num /= in;
                break;
            }

            case modulo: {
                num %= in;
                break;
            }

            case shl: {
                num = num << in;
                break; 
            }

            case shr: {
                num = num >> in;
                break;
            }


            default: {
                printf("Unsupported Operation\n");
                return 0;
            }
        }


    }

    *sum += num;
    return index;
}


int main() {
    while (1) {
        printf(">");
        char buffer[256] = {0};
        int index = 0;
        again:
        u8 key = read_key(true, false);
        if (key != 0 && key != '\n' && key != '\b') {
            buffer[index] = key;
            index = (index + 1) % 256;
            buffer[index] = '\0';
            kprintf(buffer+((index-1) % 256));
        } else if (key == '\b') {
            if (index > 0) {
                backspace();
                index--;
                buffer[index] = ' ';
                buffer[index+1] = '\0';
            }
        }
        u8 scancode = read_key(false, false);
        if (key != '\n' && scancode != 0x01) goto again;
    
        if (scancode == 0x01)
            return 0;

        printf("\n");


        long sum = 0;
        if (calc(buffer, &sum) == 0)
            continue;

        printf("%ld\n", sum);

    }
}