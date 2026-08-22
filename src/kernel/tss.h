#ifndef _KERNEL_TSS_H
#define _KERNEL_TSS_H

#include <stdint.h>

void tssInit(uint32_t kernelStack);
void tssSetEsp0(uint32_t esp0);

extern uint32_t tss_esp0;

#endif