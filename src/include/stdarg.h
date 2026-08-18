#ifndef _STDARG_H
#define _STDARG_H

typedef char* va_list;

#define va_start(ap, last) (ap = (va_list)&last + sizeof(last))
#define va_arg(ap, type)   (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap)         (ap = NULL)

#endif