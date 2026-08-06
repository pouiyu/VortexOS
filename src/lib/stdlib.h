#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

__BEGIN_DECLS

void* malloc(size_t size);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t new_size);
void free(void* ptr);

int atoi(const char* str);
long atol(const char* str);
long long atoll(const char* str);
long strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);

int rand(void);
void srand(unsigned int seed);

void exit(int status);
void abort(void);

void* bsearch(const void* key, const void* base, size_t num, size_t size,
              int (*compare)(const void*, const void*));
void qsort(void* base, size_t num, size_t size,
           int (*compare)(const void*, const void*));

int abs(int n);
long labs(long n);
long long llabs(long long n);

uint32_t getHeapStart(void);
uint32_t getHeapEnd(void);
size_t getHeapUsed(void);
size_t getHeapFree(void);

__END_DECLS

#endif