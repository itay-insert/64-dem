#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"



const char *digits = "0123456789";

static inline unsigned int asscii_to_digit(char ch) {
    for (int i = 0; i < 10; i++) {
        if (ch == digits[i])
            return i;
    }

    return 0xFFFFFFFF; // for failure
}

static inline unsigned int make_hz(char *str, unsigned int *hz) {
    unsigned int count = 0;
    *hz = 0;
    while (*str != ' ' || *str != '\0') {
        unsigned int result = asscii_to_digit(*str);
        if (result == 0xFFFFFFFF)
            return count;
        *hz = (*hz * 10) + result;
        str++;
        count++;
    }

    return count;
}


static inline void make_ms(char *str, u64 *ms) {
    *ms = 0;
    while (*str != ' ' || *str != '\0') {
        unsigned int result = asscii_to_digit(*str);
        if (result == 0xFFFFFFFF)
            return;
        *ms = (*ms * 10) + result;
        str++;
    }

}

void take_values(char *str, unsigned int *hz, u64 *ms) {
    unsigned int count = make_hz(str, hz);
    str += (count+1);
    make_ms(str, ms);
}


int main() {

    const char *commands[] = {
        "echo",
        "exit",
        "date",
        "mem",
        "shutdown",
        "reboot",
        "ls",
        "start",
        "help",
        "disksts",
        "beep"
    };



    while (1) {
        printf("A:/");
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

        u64 count = sizeof(commands) / sizeof(u64);
        for (u64 i = 0; i < count; i++) {
            int size = get_size(commands[i]);


            if (memcmp(buffer, commands[i], size) == 0) {
                switch (i) {
                    case 0 : {
                        printf("%s", buffer + size + 1);
                        goto exit;
                    }

                    case 1 : {
                        return 0;
                    }

                    case 2 : {
                        rtc_data rt = get_dateAndTime();
                        printf("%b/%b/20%b    %b:%b:%b", rt.day, rt.month, rt.year, rt.hour, rt.min, rt.sec);
                        goto exit;
                    }

                    case 3 : {
                        print_mem();
                        goto exit;
                    }

                    case 4 : {
                        acpi(true);
                    }


                    case 5 : {
                        acpi(false);
                    }

                    case 6 : {
                        ls(buffer+size+1);
                        goto exit;
                    }
                    
                    case 7 : {
                        int fd = fs_open(buffer+size+1, RD_ONLY, 0);
                        fs_start(fd);
                        fs_close(fd);
                        goto exit;
                    }

                    case 8 : {
                        for (int j = 0; j < count; j++) {
                            printf("%s \n", commands[j]);
                        }
                        goto exit;
                    }

                    case 9 : {
                        print_drives();
                        goto exit;
                    }

                    case 10 : {
                        unsigned int hz = 0;
                        u64 ms = 0;
                        take_values(buffer+size+1, &hz, &ms);
                        beep(hz, ms);
                        goto exit;
                    }
        
                }
            } else if (i == (count-1)) {
                printf("Unknown command: %s", buffer);
                break;
            }

        }

        exit:
        memset(buffer, 0, 256);

        
        printf("\n");

    }
}