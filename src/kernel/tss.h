#ifndef _KERNEL_TSS_H
#define _KERNEL_TSS_H

#include <stdint.h>

void tssInit(uint32_t kernelStack);

#endif