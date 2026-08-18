#ifndef _KERNEL_MM_PMM_H
#define _KERNEL_MM_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096

void  pmmInit(uint32_t totalMemory);
void* pmmAllocPage(void);
void  pmmFreePage(void* page);
uint32_t pmmGetFreePages(void);
uint32_t pmmGetTotalPages(void);

#endif