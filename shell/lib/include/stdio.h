#ifndef SHELL_STDIO_H
#define SHELL_STDIO_H

/* VGA formats: %d, %u, %ld, %lu, %b, %w, %ux, %lx, %s, %c, and %%. */
/* Hexadecimal output is uppercase and padded to 2, 4, 8, or 16 digits. */
int printf(const char *format, ...);


static inline int get_size(char *str) {
    int count = 0;
    while (*str != '\0') {
        str++;
        count++;
    }
    return count;
}


static inline void *memcpy(const void *dest, const void *src, unsigned long size) {
    unsigned char *destination = (unsigned char *)dest;
    unsigned char *source = (unsigned char *)src;

    for (unsigned long i = 0; i < size; i++) 
        destination[i] = source[i];

    return dest;
}



static inline int memcmp(const void *ptr0, const void *ptr1, unsigned long size) {
    unsigned char *first = (unsigned char *)ptr0;
    unsigned char *second = (unsigned char *)ptr1;


    for (unsigned long i = 0; i < size; i++) 
        if (first[i] != second[i])
            return 1;
    
    return 0;
}



static inline int str_cmp(const char *ptr0, const char *ptr1) {
    while (*ptr0 != '\0' || *ptr1 != '\0') {
        if (*ptr0 != *ptr1)
            return (unsigned char)(*ptr0 - *ptr1);
            
        ptr0++;
        ptr1++;
    }

    return (unsigned char)(*ptr0 - *ptr1);
}


static inline void *memset(const void *ptr, int value, unsigned long size) {
    unsigned char val = (unsigned char)value;
    unsigned char *buff = (unsigned char *)ptr;
    
    for (unsigned long i = 0; i < size; i++) 
        buff[i] = val;

    return ptr;
}


extern const char *digits;

static inline unsigned int ascii_to_digit(char ch) {
    for (int i = 0; i < 10; i++) {
        if (ch == digits[i])
            return i;
    }

    return 0xFFFFFFFF; // for failure
}



int mch_inchar(unsigned char *buff, int maxlen, long wtime, int tb_change_cnt) {
    if (maxlen > 256)
        return 0;

    int count = 0; 
    char key = 0;

    for (int i = 0; i < maxlen; i++) {
        if (wtime > 0) {
            for (int j = 0; j < wtime; j++) {
                count = mch_inchar(buff, maxlen, 0, tb_change_cnt);  
                if (count > 0)
                    return count;  
                sleep_ms(1);
            }
            return count;
        } else if (wtime == -1) {
            while (key == 0x00) {
                key = read_key(true, false);
            }

            buff[i] = key;
            wtime = 0;
        } else {
            key = read_key(true, false);
            if (key == 0)
                break;
            buff[i] = key;
        }

        count++;
    }

    return count;
}

#endif
