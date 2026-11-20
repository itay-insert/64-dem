// Single-process C library support used by the tiny 64-dem Vim build.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <limits.h>
#include <stddef.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <termios.h>
#include <utime.h>
#include <unistd.h>
#include <wctype.h>

extern void *dem_alloc(size_t);
extern void dem_free(void *);
extern void sleep_ms(uint64_t);
extern uint64_t getshellsize(void);
extern int dem_keyboard_ready(void);
extern int vim_vsnprintf(char *, size_t, const char *, va_list);
extern void _exit(int);

typedef struct {
    uint8_t year, month, day, hour, minute, second;
} rtc_data;
extern rtc_data get_dateAndTime(void);

static int error_number;
int *__errno_location(void) { return &error_number; }

typedef struct {
    void *raw;
    size_t size;
} allocation_header;

void *malloc(size_t size)
{
    const size_t alignment = _Alignof(max_align_t);
    const size_t overhead = sizeof(allocation_header) + alignment - 1;
    if (size > SIZE_MAX - overhead) {
        errno = ENOMEM;
        return NULL;
    }
    void *raw = dem_alloc(size + overhead);
    if (!raw) {
        errno = ENOMEM;
        return NULL;
    }
    uintptr_t address = ((uintptr_t)raw + overhead) & ~(alignment - 1);
    allocation_header *allocation = (allocation_header *)address - 1;
    *allocation = (allocation_header){.raw = raw, .size = size};
    return (void *)address;
}

void free(void *ptr)
{
    if (ptr)
        dem_free(((allocation_header *)ptr - 1)->raw);
}

void *realloc(void *ptr, size_t size)
{
    if (!size) {
        free(ptr);
        return NULL;
    }
    void *result = malloc(size);
    if (result && ptr) {
        size_t old_size = ((allocation_header *)ptr - 1)->size;
        memcpy(result, ptr, old_size < size ? old_size : size);
        free(ptr);
    }
    return result;
}

void *calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    void *result = malloc(count * size);
    if (result)
        memset(result, 0, count * size);
    return result;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    if ((uintptr_t)d > (uintptr_t)s) {
        while (n) {
            --n;
            d[n] = s[n];
        }
    } else {
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i];
    }
    return dest;
}

void *memchr(const void *ptr, int ch, size_t n)
{
    const unsigned char *bytes = ptr;
    for (size_t i = 0; i < n; ++i)
        if (bytes[i] == (unsigned char)ch)
            return (void *)(bytes + i);
    return NULL;
}

size_t strlen(const char *s)
{
    const char *end = s;
    while (*end)
        ++end;
    return end - s;
}

char *stpcpy(char *dest, const char *src)
{
    while ((*dest = *src++) != '\0')
        ++dest;
    return dest;
}

char *strcpy(char *dest, const char *src)
{
    stpcpy(dest, src);
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t length = 0;
    while (length < n && src[length]) {
        dest[length] = src[length];
        ++length;
    }
    memset(dest + length, 0, n - length);
    return dest;
}

char *strcat(char *dest, const char *src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a == *b && *a) {
        ++a;
        ++b;
        --n;
    }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

int strcmp(const char *a, const char *b) { return strncmp(a, b, SIZE_MAX); }
int strcoll(const char *a, const char *b) { return strcmp(a, b); }

static int ascii_lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
static int ascii_upper(int ch) { return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch; }

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b) && *a) {
        ++a;
        ++b;
        --n;
    }
    return n ? ascii_lower((unsigned char)*a) - ascii_lower((unsigned char)*b) : 0;
}

int strcasecmp(const char *a, const char *b) { return strncasecmp(a, b, SIZE_MAX); }

char *strchr(const char *s, int ch)
{
    do {
        if ((unsigned char)*s == (unsigned char)ch)
            return (char *)s;
    } while (*s++);
    return NULL;
}

char *strpbrk(const char *s, const char *set)
{
    while (*s && !strchr(set, *s))
        ++s;
    return *s ? (char *)s : NULL;
}

char *strstr(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    if (!n)
        return (char *)s;
    for (; *s; ++s)
        if (!strncmp(s, needle, n))
            return (char *)s;
    return NULL;
}

char *strtok(char *s, const char *delimiters)
{
    static char *next;
    s = s ? s : next;
    if (!s)
        return NULL;
    while (*s && strchr(delimiters, *s))
        ++s;
    if (!*s)
        return next = NULL;
    next = strpbrk(s, delimiters);
    if (next)
        *next++ = '\0';
    return s;
}

static unsigned short char_class[384];
static int lower_case[384], upper_case[384];
static const unsigned short *class_table = char_class + 128;
static const int *lower_table = lower_case + 128, *upper_table = upper_case + 128;

static void init_ctype(void)
{
    for (int i = -128; i <= 255; ++i) {
        int c = (unsigned char)i;
        bool digit = c >= '0' && c <= '9';
        bool upper = c >= 'A' && c <= 'Z';
        bool lower = c >= 'a' && c <= 'z';
        bool space = c == ' ' || (c >= '\t' && c <= '\r');
        bool print = c >= ' ' && c <= '~';
        bool alpha = upper || lower;
        char_class[i + 128] = (digit ? _ISdigit : 0) | (upper ? _ISupper : 0)
            | (lower ? _ISlower : 0) | (space ? _ISspace : 0) | (print ? _ISprint : 0)
            | (alpha ? _ISalpha : 0) | ((alpha || digit) ? _ISalnum : 0)
            | ((digit || (ascii_lower(c) >= 'a' && ascii_lower(c) <= 'f')) ? _ISxdigit : 0)
            | ((print && c != ' ') ? _ISgraph : 0) | ((c < ' ' || c == 127) ? _IScntrl : 0)
            | ((c == ' ' || c == '\t') ? _ISblank : 0)
            | ((print && !alpha && !digit && !space) ? _ISpunct : 0);
        lower_case[i + 128] = i == EOF ? EOF : ascii_lower(c);
        upper_case[i + 128] = i == EOF ? EOF : ascii_upper(c);
    }
}

const unsigned short int **__ctype_b_loc(void) { return &class_table; }
const int **__ctype_tolower_loc(void) { return &lower_table; }
const int **__ctype_toupper_loc(void) { return &upper_table; }
wint_t towlower(wint_t ch) { return ch < 128 ? (wint_t)ascii_lower(ch) : ch; }
wint_t towupper(wint_t ch) { return ch < 128 ? (wint_t)ascii_upper(ch) : ch; }
int iswupper(wint_t ch) { return towlower(ch) != ch; }

long strtol(const char *s, char **end, int base)
{
    const char *start = s;
    while (isspace((unsigned char)*s))
        ++s;
    bool negative = *s == '-';
    if (*s == '+' || *s == '-')
        ++s;
    if ((!base || base == 16) && s[0] == '0' && ascii_lower(s[1]) == 'x' &&
        isxdigit((unsigned char)s[2])) {
        s += 2;
        base = 16;
    }
    if (!base)
        base = *s == '0' ? 8 : 10;
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end)
            *end = (char *)start;
        return 0;
    }
    const char *first_digit = s;
    unsigned long value = 0;
    unsigned long limit = (unsigned long)LONG_MAX + negative;
    while (*s) {
        unsigned char c = ascii_lower((unsigned char)*s);
        int digit = c >= 'a' && c <= 'z' ? c - 'a' + 10 : c - '0';
        if ((!isdigit(c) && !(c >= 'a' && c <= 'z')) || digit < 0 || digit >= base)
            break;
        if (value > (limit - digit) / base) {
            value = limit;
            errno = ERANGE;
        } else {
            value = value * base + digit;
        }
        ++s;
    }
    if (end)
        *end = (char *)(s == first_digit ? start : s);
    return negative ? (long)(0UL - value) : (long)value;
}

static FILE streams[3] = {{._fileno = 0}, {._fileno = 1}, {._fileno = 2}};
FILE *stdin = &streams[0], *stdout = &streams[1], *stderr = &streams[2];

FILE *fdopen(int fd, const char *mode)
{
    (void)mode;
    if (fd < 0)
        return NULL;
    FILE *stream = calloc(1, sizeof(*stream));
    if (stream)
        stream->_fileno = fd;
    return stream;
}

FILE *fopen(const char *name, const char *mode)
{
    int flags = *mode == 'r' ? O_RDONLY : O_WRONLY | O_CREAT;
    if (*mode == 'w')
        flags |= O_TRUNC;
    if (*mode == 'a')
        flags |= O_APPEND;
    if (strchr(mode, '+'))
        flags = (flags & ~O_ACCMODE) | O_RDWR;
    int fd = open(name, flags, 0666);
    FILE *stream = fdopen(fd, mode);
    if (!stream && fd >= 0)
        close(fd);
    return stream;
}

int fileno(FILE *stream) { return stream->_fileno; }
int fflush(FILE *stream) { (void)stream; return 0; }
int fclose(FILE *stream)
{
    int fd = fileno(stream);
    if (fd < 3)
        return 0;
    int result = close(fd);
    free(stream);
    return result;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    if (!size || count > SIZE_MAX / size)
        return 0;
    ssize_t n = read(fileno(stream), buffer, size * count);
    return n > 0 ? (size_t)n / size : 0;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    if (!size || count > SIZE_MAX / size)
        return 0;
    ssize_t n = write(fileno(stream), buffer, size * count);
    return n > 0 ? (size_t)n / size : 0;
}

int fgetc(FILE *stream)
{
    unsigned char byte;
    return fread(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int getc(FILE *stream) { return fgetc(stream); }

char *fgets(char *s, int n, FILE *stream)
{
    int length = 0;
    while (length < n - 1) {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        s[length++] = c;
        if (c == '\n')
            break;
    }
    if (!length)
        return NULL;
    s[length] = '\0';
    return s;
}

int fputc(int c, FILE *stream)
{
    unsigned char byte = c;
    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int putc(int c, FILE *stream) { return fputc(c, stream); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *stream)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, stream) == n ? 0 : EOF;
}

int puts(const char *s) { return fputs(s, stdout) == EOF ? EOF : putchar('\n'); }
int fseeko(FILE *stream, off_t offset, int whence)
{
    return lseek(fileno(stream), offset, whence) < 0 ? -1 : 0;
}
off_t ftello(FILE *stream) { return lseek(fileno(stream), 0, SEEK_CUR); }
int fseek(FILE *stream, long offset, int whence) { return fseeko(stream, offset, whence); }
long ftell(FILE *stream) { return ftello(stream); }
void rewind(FILE *stream) { (void)fseek(stream, 0, SEEK_SET); }

int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    (void)stream;
    (void)buffer;
    (void)mode;
    (void)size;
    return 0;
}

static int print_to(FILE *stream, const char *format, va_list ap)
{
    char buffer[512];
    va_list args;
    va_copy(args, ap);
    int n = vim_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (n < 0)
        return n;
    char *output = buffer;
    if ((size_t)n >= sizeof(buffer)) {
        output = malloc((size_t)n + 1);
        if (!output)
            return -1;
        vim_vsnprintf(output, (size_t)n + 1, format, ap);
    }
    int result = fwrite(output, 1, n, stream) == (size_t)n ? n : -1;
    if (output != buffer)
        free(output);
    return result;
}

int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = print_to(stdout, format, args);
    va_end(args);
    return n;
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = print_to(stream, format, args);
    va_end(args);
    return n;
}

// Vim's formatter delegates the basic integer conversions to sprintf().
static void print_integer(char **dest, unsigned long long n, unsigned base, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[64];
    unsigned length = 0;
    do {
        reversed[length++] = digits[n % base];
        n /= base;
    } while (n);
    while (length)
        *(*dest)++ = reversed[--length];
}

int sprintf(char *dest, const char *format, ...)
{
    char *out = dest;
    va_list args;
    va_start(args, format);
    while (*format) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        ++format;
        char fill = *format == '0' ? '0' : ' ';
        unsigned width = 0;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');
        unsigned length = 0;
        while (*format == 'l') {
            ++length;
            ++format;
        }
        bool short_argument = *format == 'h';
        if (short_argument)
            ++format;
        char spec = *format;
        if (!spec)
            break;
        ++format;
        if (spec == '%' || spec == 'c') {
            *out++ = spec == '%' ? '%' : (char)va_arg(args, int);
            continue;
        }
        if (spec == 's') {
            out = stpcpy(out, va_arg(args, const char *));
            continue;
        }
        char number[64], *end = number;
        unsigned long long magnitude;
        if (spec == 'd' || spec == 'i') {
            long long value = length == 2 ? va_arg(args, long long)
                : length == 1 ? va_arg(args, long) : va_arg(args, int);
            if (short_argument)
                value = (short)value;
            magnitude = value;
            if (value < 0) {
                *out++ = '-';
                magnitude = 0ULL - magnitude;
                if (width)
                    --width;
            }
        } else if (spec == 'p') {
            magnitude = (uintptr_t)va_arg(args, void *);
            *out++ = '0';
            *out++ = 'x';
        } else if (strchr("uoxX", spec)) {
            magnitude = length == 2 ? va_arg(args, unsigned long long)
                : length == 1 ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            if (short_argument)
                magnitude = (unsigned short)magnitude;
        } else {
            *out++ = '?';
            continue;
        }
        unsigned base = spec == 'o' ? 8 : strchr("xXp", spec) ? 16 : 10;
        print_integer(&end, magnitude, base, spec == 'X');
        while (width > (unsigned)(end - number)) {
            *out++ = fill;
            --width;
        }
        memcpy(out, number, end - number);
        out += end - number;
    }
    *out = '\0';
    va_end(args);
    return out - dest;
}

void __assert_fail(const char *expr, const char *file, unsigned line, const char *func)
{
    fprintf(stderr, "%s:%u: %s: assertion failed: %s\n", file, line, func, expr);
    _exit(1);
}

void dem_vim_init(void) { init_ctype(); }

static bool leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int days_in_month(int year, int month)
{
    return month_days[month] + (month == 1 && leap_year(year));
}

static int from_bcd(uint8_t value)
{
    return (value >> 4) * 10 + (value & 0x0F);
}

time_t time(time_t *value)
{
    rtc_data rtc = get_dateAndTime();
    int short_year = from_bcd(rtc.year);
    int year = short_year >= 80 ? 1900 + short_year : 2000 + short_year;
    int month = from_bcd(rtc.month);
    int day = from_bcd(rtc.day);
    time_t days = 0;
    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month - 1)) {
        errno = EIO;
        return -1;
    }
    for (int y = 1970; y < year; ++y)
        days += 365 + leap_year(y);
    for (int m = 0; m + 1 < month; ++m)
        days += days_in_month(year, m);
    days += day - 1;
    time_t result = days * 86400 + from_bcd(rtc.hour) * 3600
        + from_bcd(rtc.minute) * 60 + from_bcd(rtc.second);
    if (value)
        *value = result;
    return result;
}

struct tm *localtime_r(const time_t *value, struct tm *result)
{
    *result = (struct tm){0};
    time_t days = *value / 86400;
    int seconds = *value % 86400;
    if (seconds < 0) {
        --days;
        seconds += 86400;
    }
    result->tm_hour = seconds / 3600;
    result->tm_min = seconds / 60 % 60;
    result->tm_sec = seconds % 60;
    result->tm_wday = (days % 7 + 11) % 7;
    int year = 1970;
    while (days < 0) {
        --year;
        days += 365 + leap_year(year);
    }
    while (days >= 365 + leap_year(year)) {
        days -= 365 + leap_year(year);
        ++year;
    }
    result->tm_year = year - 1900;
    result->tm_yday = days;
    int month = 0;
    while (days >= days_in_month(year, month))
        days -= days_in_month(year, month++);
    result->tm_mon = month;
    result->tm_mday = days + 1;
    result->tm_zone = "UTC";
    return result;
}

int gettimeofday(struct timeval *value, void *zone)
{
    (void)zone;
    value->tv_sec = time(NULL);
    value->tv_usec = 0;
    return value->tv_sec < 0 ? -1 : 0;
}

void tzset(void) {}

double log10(double value)
{
    double result;
    __asm__("fldlg2; fxch; fyl2x" : "=t"(result) : "0"(value));
    return result;
}

double pow(double value, double exponent)
{
    if (exponent == 0 || value == 1)
        return 1;
    if (value == 0)
        return exponent > 0 ? 0 : __builtin_inf();
    bool negative = value < 0;
    bool odd = false;
    if (negative) {
        if (exponent <= INT64_MIN || exponent >= (double)INT64_MAX ||
            (double)(int64_t)exponent != exponent)
            return __builtin_nan("");
        odd = (int64_t)exponent & 1;
        value = -value;
    }
    double power;
    __asm__("fyl2x" : "=t"(power) : "0"(value), "u"(exponent) : "st(1)");
    double result;
    __asm__("fld %%st(0); frndint; fxch; fsub %%st(1), %%st; f2xm1;"
            "fld1; faddp; fscale; fstp %%st(1)" : "=t"(result) : "0"(power));
    return odd ? -result : result;
}

double strtod(const char *s, char **end)
{
    const char *start = s;
    while (isspace((unsigned char)*s))
        ++s;
    bool negative = *s == '-';
    if (*s == '-' || *s == '+')
        ++s;
    double value = 0, place = 0;
    bool digits = false;
    while (isdigit((unsigned char)*s) || (!place && *s == '.')) {
        if (*s == '.') {
            place = 0.1;
        } else {
            digits = true;
            value = place ? value + (*s - '0') * place : value * 10 + (*s - '0');
            if (place)
                place /= 10;
        }
        ++s;
    }
    if (digits && (*s == 'e' || *s == 'E')) {
        char *next;
        long exponent = strtol(s + 1, &next, 10);
        if (next != s + 1) {
            value *= pow(10, exponent);
            s = next;
        }
    }
    if (end)
        *end = (char *)(digits ? s : start);
    return negative ? -value : value;
}

void qsort(void *data, size_t count, size_t width, int (*compare)(const void *, const void *))
{
    // Shell sort is adequate for the editor's small collections.
    unsigned char *bytes = data;
    for (size_t gap = count / 2; gap; gap /= 2) {
        for (size_t i = gap; i < count; ++i) {
            for (size_t j = i; j >= gap; j -= gap) {
                unsigned char *left = bytes + (j - gap) * width;
                unsigned char *right = bytes + j * width;
                if (compare(left, right) <= 0)
                    break;
                for (size_t k = 0; k < width; ++k) {
                    unsigned char byte = left[k];
                    left[k] = right[k];
                    right[k] = byte;
                }
            }
        }
    }
}

char **environ;
char *getenv(const char *name)
{
    size_t n = strlen(name);
    for (char **entry = environ; entry && *entry; ++entry)
        if (!strncmp(*entry, name, n) && (*entry)[n] == '=')
            return *entry + n + 1;
    return NULL;
}

int unsetenv(const char *name)
{
    size_t n = strlen(name);
    char **out = environ;
    if (out) {
        for (char **entry = environ; *entry; ++entry)
            if (strncmp(*entry, name, n) || (*entry)[n] != '=')
                *out++ = *entry;
        *out = NULL;
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name))
        return 0;
    char *entry = malloc(strlen(name) + strlen(value) + 2);
    if (!entry)
        return -1;
    strcpy(stpcpy(stpcpy(entry, name), "="), value);
    size_t count = 0;
    if (environ)
        while (environ[count])
            ++count;
    char **next = realloc(environ, (count + 2) * sizeof(*next));
    if (!next) {
        free(entry);
        return -1;
    }
    environ = next;
    environ[count] = NULL;
    // getenv pointers to existing entries remain valid for the process lifetime.
    unsetenv(name);
    count = 0;
    while (environ[count])
        ++count;
    environ[count] = entry;
    environ[count + 1] = NULL;
    return 0;
}

char *setlocale(int category, const char *locale)
{
    (void)category;
    return !locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX") ? "C" : NULL;
}

int mblen(const char *s, size_t n)
{
    return !s || !*s ? 0 : n && (unsigned char)*s < 128 ? 1 : -1;
}

char *strerror(int error)
{
    switch (error) {
        case 0: return "Success";
        case EACCES: return "Permission denied";
        case EBADF: return "Bad file descriptor";
        case EEXIST: return "File exists";
        case EINVAL: return "Invalid argument";
        case EIO: return "I/O error";
        case EISDIR: return "Is a directory";
        case ENOENT: return "No such file or directory";
        case ENOMEM: return "Out of memory";
        case ENOSPC: return "No space left";
        case ENOSYS: return "Operation unavailable on 64-dem";
        case ENOTDIR: return "Not a directory";
        default: return "Unknown error";
    }
}

int access(const char *path, int mode)
{
    struct stat info;
    if (stat(path, &info))
        return -1;
    if (((mode & R_OK) && !(info.st_mode & S_IRUSR)) ||
        ((mode & W_OK) && !(info.st_mode & S_IWUSR)) ||
        ((mode & X_OK) && !(info.st_mode & S_IXUSR))) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int isatty(int fd) { return fd >= 0 && fd < 3; }

int ioctl(int fd, unsigned long request, ...)
{
    if (isatty(fd) && request == TIOCGWINSZ) {
        va_list args;
        va_start(args, request);
        struct winsize *size = va_arg(args, struct winsize *);
        va_end(args);
        uint64_t dimensions = getshellsize();
        *size = (struct winsize){.ws_row = dimensions >> 32, .ws_col = (uint32_t)dimensions};
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

int select(int n, fd_set *reads, fd_set *writes, fd_set *errors, struct timeval *timeout)
{
    bool keyboard = n > 0 && reads && FD_ISSET(0, reads);
    if (!keyboard && !timeout) {
        errno = ENOSYS;
        return -1;
    }
    uint64_t ms = timeout ? (uint64_t)timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000 : 0;
    if (reads)
        FD_ZERO(reads);
    if (writes)
        FD_ZERO(writes);
    if (errors)
        FD_ZERO(errors);
    for (;;) {
        if (keyboard && dem_keyboard_ready()) {
            FD_SET(0, reads);
            return 1;
        }
        if (timeout && !ms)
            return 0;
        sleep_ms(1);
        if (timeout)
            --ms;
    }
}

ssize_t readlink(const char *path, char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    struct stat info;
    if (!stat(path, &info))
        errno = EINVAL;
    return -1;
}

char *getcwd(char *buffer, size_t size)
{
    // Programs launched from the shell normally live in the FAT SHELL directory.
    const char *directory = access("/SHELL", F_OK) == 0 ? "/SHELL" : "/";
    size_t needed = strlen(directory) + 1;
    if (!buffer) {
        buffer = malloc(needed);
        size = needed;
    }
    if (!buffer)
        return NULL;
    if (size < needed) {
        errno = ERANGE;
        return NULL;
    }
    return memcpy(buffer, directory, needed);
}

int uname(struct utsname *name)
{
    *name = (struct utsname){0};
    strcpy(name->sysname, "64-dem");
    strcpy(name->nodename, "64-dem");
    strcpy(name->machine, "x86_64");
    return 0;
}

int sysinfo(struct sysinfo *info) { (void)info; errno = ENOSYS; return -1; }
pid_t getpid(void) { return 1; }
uid_t getuid(void) { return 0; }
gid_t getgid(void) { return 0; }

static int unavailable(void) { errno = ENOSYS; return -1; }

int chdir(const char *path) { return !strcmp(path, ".") ? 0 : unavailable(); }
int fchdir(int fd) { (void)fd; return unavailable(); }
int chmod(const char *path, mode_t mode) { (void)path; (void)mode; return unavailable(); }
int fstat(int fd, struct stat *info) { (void)fd; (void)info; return unavailable(); }
int unlink(const char *path) { (void)path; return unavailable(); }
int rmdir(const char *path) { (void)path; return unavailable(); }
int symlink(const char *from, const char *to) { (void)from; (void)to; return unavailable(); }
int rename(const char *from, const char *to) { (void)from; (void)to; return unavailable(); }
int dup(int fd) { (void)fd; return unavailable(); }
int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return unavailable(); }
int pipe(int fds[2]) { (void)fds; return unavailable(); }
pid_t fork(void) { return unavailable(); }
pid_t setsid(void) { return unavailable(); }
int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; return unavailable(); }
pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)status;
    (void)options;
    errno = ECHILD;
    return -1;
}
int kill(pid_t pid, int sig) { (void)pid; (void)sig; return unavailable(); }
int raise(int sig) { (void)sig; return unavailable(); }
int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    (void)how;
    (void)set;
    if (old)
        memset(old, 0, sizeof(*old));
    return 0;
}
void (*signal(int sig, void (*handler)(int)))(int)
{
    (void)sig;
    (void)handler;
    return SIG_DFL;
}

DIR *opendir(const char *path) { (void)path; unavailable(); return NULL; }
struct dirent *readdir(DIR *dir) { (void)dir; unavailable(); return NULL; }
int closedir(DIR *dir) { (void)dir; return unavailable(); }
int dirfd(DIR *dir) { (void)dir; return unavailable(); }
char *mkdtemp(char *name) { (void)name; unavailable(); return NULL; }
struct passwd *getpwent(void) { return NULL; }
struct passwd *getpwnam(const char *name) { (void)name; return NULL; }
void setpwent(void) {}
void endpwent(void) {}
void sync(void) {}

long sysconf(int name)
{
    if (name == _SC_PAGESIZE)
        return 4096;
    return unavailable();
}

mode_t umask(mode_t mode)
{
    static mode_t current;
    mode_t previous = current;
    current = mode & 0777;
    return previous;
}

int tcgetattr(int fd, struct termios *settings)
{
    if (!isatty(fd)) {
        errno = ENOTTY;
        return -1;
    }
    *settings = (struct termios){0};
    settings->c_cflag = CS8 | CREAD | B9600;
    settings->c_ispeed = B9600;
    settings->c_ospeed = B9600;
    settings->c_cc[VINTR] = 3;
    settings->c_cc[VEOF] = 4;
    settings->c_cc[VERASE] = 8;
    settings->c_cc[VMIN] = 1;
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *settings)
{
    (void)action;
    (void)settings;
    if (isatty(fd))
        return 0;
    errno = ENOTTY;
    return -1;
}

int tcflush(int fd, int queue)
{
    return tcsetattr(fd, queue, NULL);
}

int sigemptyset(sigset_t *set)
{
    memset(set, 0, sizeof(*set));
    return 0;
}

int sigaddset(sigset_t *set, int sig)
{
    if (sig < 1 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    ((unsigned char *)set)[(sig - 1) / 8] |= 1u << ((sig - 1) % 8);
    return 0;
}

int sigismember(const sigset_t *set, int sig)
{
    if (sig < 1 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    return (((const unsigned char *)set)[(sig - 1) / 8] >> ((sig - 1) % 8)) & 1;
}

int utime(const char *path, const struct utimbuf *times)
{
    (void)path;
    (void)times;
    return unavailable();
}

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }

double floor(double value)
{
    if (!(value > (double)INT64_MIN && value < (double)INT64_MAX))
        return value;
    double integral = (double)(int64_t)value;
    return integral > value ? integral - 1 : integral;
}

double ceil(double value)
{
    if (!(value > (double)INT64_MIN && value < (double)INT64_MAX))
        return value;
    double integral = (double)(int64_t)value;
    return integral < value ? integral + 1 : integral;
}

int usleep(useconds_t microseconds)
{
    sleep_ms(((uint64_t)microseconds + 999) / 1000);
    return 0;
}

int link(const char *source, const char *target)
{
    (void)source;
    (void)target;
    return unavailable();
}
