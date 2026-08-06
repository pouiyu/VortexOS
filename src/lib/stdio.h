#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <io.h>
#include <vga.h>
#include <sys/cdefs.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

typedef struct {
    int fd;
} FILE;

#define stdin  ((FILE*)STDIN_FILENO)
#define stdout ((FILE*)STDOUT_FILENO)
#define stderr ((FILE*)STDERR_FILENO)

#define EOF (-1)

__BEGIN_DECLS



__END_DECLS

#endif