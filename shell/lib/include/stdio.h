#ifndef SHELL_STDIO_H
#define SHELL_STDIO_H

/* VGA formats: %d, %u, %ld, %lu, %b, %w, %ux, %lx, %s, %c, and %%. */
/* Hexadecimal output is uppercase and padded to 2, 4, 8, or 16 digits. */
int printf(const char *format, ...);

#endif
