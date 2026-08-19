#ifndef _KERNEL_USER_H
#define _KERNEL_USER_H

#include <stdint.h>

void jumpToUserMode(void* entry);
uint32_t sysRead(char* buf);
uint32_t sysWrite(const char* str);
void sysExit(void);
void userTest(void);

#endif