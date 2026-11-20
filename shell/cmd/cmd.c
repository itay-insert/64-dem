#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


static inline unsigned int make_hz(char *str, unsigned int *hz) {
    unsigned int count = 0;
    *hz = 0;
    while (*str != ' ' || *str != '\0') {
        unsigned int result = ascii_to_digit(*str);
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
        unsigned int result = ascii_to_digit(*str);
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
        if (key >= ' ' && key < 0x7f) {
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
        if (key == 0x1b)
            return 0;
        if (key != '\n')
            goto again;

        printf("\n");

        char *command = buffer + (buffer[0] == '/');
        u64 count = sizeof(commands) / sizeof(u64);
        for (u64 i = 0; i < count; i++) {
            int size = get_size(commands[i]);


            if (memcmp(command, commands[i], size) == 0 &&
                (command[size] == ' ' || command[size] == '\0')) {
                switch (i) {
                    case 0 : {
                        printf("%s", command + size + (command[size] == ' '));
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
                        ls(command + size + (command[size] == ' '));
                        goto exit;
                    }
                    
                    case 7 : {
                        char *program = command + size;
                        while (*program == ' ')
                            ++program;
                        if (!*program) {
                            printf("Usage: start PROGRAM [FILENAME]");
                            goto exit;
                        }
                        char *argument = program;
                        while (*argument && *argument != ' ')
                            ++argument;
                        if (*argument) {
                            *argument++ = '\0';
                            while (*argument == ' ')
                                ++argument;
                        }
                        int fd = fs_open(program, RD_ONLY, 0);
                        if (fd < 0) {
                            printf("Cannot open %s", program);
                            goto exit;
                        }
                        fs_start(fd, *argument ? (u64)argument : 0);
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
                        take_values(command + size + (command[size] == ' '), &hz, &ms);
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
