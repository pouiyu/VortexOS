#ifndef _KERNEL_MM_PAGING_H
#define _KERNEL_MM_PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_WRITABLE 0x02
#define PAGE_USER     0x04

void pagingInit(void);
void pagingMapPage(uint32_t virtualAddr, uint32_t physAddr, uint32_t flags);
void pagingUnmapPage(uint32_t virtualAddr);

#endif