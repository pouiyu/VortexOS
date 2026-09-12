#ifndef _KERNEL_MM_PAGING_H
#define _KERNEL_MM_PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_WRITABLE 0x02
#define PAGE_USER     0x04
#define PAGE_PWT      0x08   /* Page Write-Through：AHCI DMA 缓冲一致性 */
#define PAGE_PCD      0x10   /* Page Cache Disable：AHCI DMA 缓冲非缓存 */

void pagingInit(void);
void pagingMapPage(uint32_t virtualAddr, uint32_t physAddr, uint32_t flags);
void pagingUnmapPage(uint32_t virtualAddr);
uint32_t pagingCreateUserDirectory(void);

extern uint32_t* pageDir;

#endif