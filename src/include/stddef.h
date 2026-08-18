#ifndef _STDDEF_H
#define _STDDEF_H

#include <stdint.h>

#define NULL ((void*)0)

typedef uint32_t size_t;   // 如果 stdint.h 里没定义
typedef int32_t  ptrdiff_t;

#endif