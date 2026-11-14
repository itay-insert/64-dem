#include <stdarg.h>
#include <stddef.h>
#include "stdio.h"
#include "sys.h"

int printf(const char *format, ...) {
    char output[256];
    size_t used = 0;
    int count = 0;
    va_list args;
    va_start(args, format);

    /* Bounded stack buffering avoids one syscall per character and malloc. */
#define EMIT(value) do {                         \
    char emitted = (char)(value);                \
    if (emitted != '\0') {                       \
        output[used++] = emitted;                \
        ++count;                                \
        if (used == sizeof(output) - 1) {        \
            output[used] = '\0';                \
            kprintf(output);                    \
            used = 0;                           \
        }                                       \
    }                                           \
} while (0)

    if (!format) format = "(null)";
    while (*format) {
        if (*format != '%') {
            EMIT(*format++);
            continue;
        }

        const char *specifier = format++;
        unsigned long number = 0;
        unsigned base = 10;
        unsigned width = 0;
        int negative = 0;

        switch (*format) {
        case 's': {
            const char *string = va_arg(args, const char *);
            if (!string) string = "(null)";
            while (*string) EMIT(*string++);
            ++format;
            continue;
        }
        case 'c':
            EMIT(va_arg(args, int));
            ++format;
            continue;
        case '%':
            EMIT('%');
            ++format;
            continue;
        case 'd': {
            int value = va_arg(args, int);
            negative = value < 0;
            number = negative ? 0u - (unsigned)value : (unsigned)value;
            break;
        }
        case 'u':
            number = va_arg(args, unsigned int);
            if (format[1] == 'x') {
                ++format;
                base = 16;
                width = 8;
            }
            break;
        case 'b':
            number = (unsigned char)va_arg(args, int);
            base = 16;
            width = 2;
            break;
        case 'w':
            number = (unsigned short)va_arg(args, int);
            base = 16;
            width = 4;
            break;
        case 'l':
            if (format[1] == 'd') {
                long value = va_arg(args, long);
                negative = value < 0;
                number = negative ? 0ul - (unsigned long)value : (unsigned long)value;
                ++format;
            } else if (format[1] == 'u') {
                number = va_arg(args, unsigned long);
                ++format;
            } else if (format[1] == 'x') {
                number = va_arg(args, u64);
                base = 16;
                width = 16;
                ++format;
            } else {
                EMIT('%');
                EMIT('l');
                ++format;
                continue;
            }
            break;
        default:
            /* Keep unsupported formats literal, including a trailing %. */
            EMIT(*specifier);
            continue;
        }

        ++format;
        if (negative) EMIT('-');
        char digits[20]; // Maximum unsigned 64-bit decimal length.
        unsigned length = 0;
        do {
            digits[length++] = "0123456789ABCDEF"[number % base];
            number /= base;
        } while (number);
        while (length < width) digits[length++] = '0';
        while (length) EMIT(digits[--length]);
    }

    if (used) {
        output[used] = '\0';
        kprintf(output);
    }
    va_end(args);
#undef EMIT
    return count;
}
