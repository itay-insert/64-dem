#include <stddef.h>
#include <stdbool.h>
#include "uint_definitions.h"
#include "sys.h"
#include "stdio.h"


u64 *cmd_history = NULL;
u16 command_count = 0;
u16 cmd_index = 0;

u16 user_index = 0xFFFF;

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


static void increase_cmds(char *cmd, int size) {
    char *new_cmd = (char *)malloc(size);
    memcpy(new_cmd, cmd, size);

    new_cmd[size] = '\0';

    if (cmd_history == NULL || command_count == 0) {
        cmd_history = (u64 *)malloc(256<<3);
        command_count = 256;
        memset(cmd_history, 0 , 256<<3);
        cmd_history[0] = (u64)new_cmd;
        cmd_index = 1;
        return;
    }


    if (cmd_index == command_count)
        cmd_index = 0;

    if (cmd_history[cmd_index]) 
        free((void *)cmd_history[cmd_index]);

    cmd_history[cmd_index] = (u64)new_cmd;
    cmd_index++;
}

static inline void dellocate() {
    if (cmd_history) {
        for (int i = 0; i < command_count; i++) {
            if (cmd_history[i])
                free((void *)cmd_history[i]);
        }

        free(cmd_history);
    }
}

#define UP 0x48
#define LEFT 0x4B
#define RIGHT 0x4D
#define DOWN 0x50

static void inc_strings(char *str0, char *str1, int size0, int size1) {
    char *new_str = (char *)malloc(size0+size1);
    memcpy(new_str, str0, size0);
    memcpy(new_str+size0, str1, size1);
    increase_cmds(new_str, size0+size1);
    free(new_str);   
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
        u8 scancode = read_key(false, false);

        switch (scancode) {
            case UP: {
                if (cmd_history == NULL)
                    break;
                
                if (user_index == 0xFFFF) {
                    user_index = cmd_index - 1;
                } else {
                    if (user_index > 0)
                        user_index--;
                    else 
                        user_index = cmd_index;
                }

                if (cmd_history[user_index]) {
                    char *former_cmd = (char *)cmd_history[user_index];

                    int cmd_size = get_size(former_cmd);
                    memcpy(buffer, former_cmd, (u64)cmd_size);
                    buffer[cmd_size] = '\0';
                    printf(buffer);
                    MoveCursor(-cmd_size, 7);
                }


                break;
            }


            case DOWN : {
                if (cmd_history == NULL)
                    break;


                if (user_index == 0xFFFF) {
                    user_index = cmd_index - 1;
                } else if (user_index < cmd_index) 
                    user_index++;

                if (cmd_history[user_index]) {
                    char *former_cmd = (char *)cmd_history[user_index];

                    int cmd_size = get_size(former_cmd);
                    memcpy(buffer, former_cmd, (u64)cmd_size);
                    buffer[cmd_size] = '\0';
                    printf(buffer);
                    MoveCursor(-cmd_size, 7);
                }

                break;
            }


            default: {
                break;
            }
        }

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
        if (key == 0x1b) {
            dellocate();
            return 0;
        }

        if (key != '\n')
            goto again;

        user_index = 0xFFFF;

        printf("\n");

        char *command = buffer + (buffer[0] == '/');
        u64 count = sizeof(commands) / sizeof(u64);
        for (u64 i = 0; i < count; i++) {
            int size = get_size(commands[i]);



            if (memcmp(command, commands[i], size) == 0 &&
                (command[size] == ' ' || command[size] == '\0')) {
                inc_strings(command, command+size, size, get_size(command+size));
                switch (i) {
                    case 0 : {
                        printf("%s", command + size + (command[size] == ' '));
                        goto exit;
                    }

                    case 1 : {
                        dellocate();
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
