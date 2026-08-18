#include "paging.h"
#include "pmm.h"
#include <string/string.h>

#define PAGE_DIR_INDEX(addr)   (((uint32_t)(addr) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(addr) (((uint32_t)(addr) >> 12) & 0x3FF)

static uint32_t* pageDir = NULL;

void pagingInit(void) {
    pageDir = (uint32_t*)pmmAllocPage();
    memset(pageDir, 0, PAGE_SIZE);

    // 映射前 256MB（假设 256MB 内存）
    uint32_t* pageTable;
    for (uint32_t dirIndex = 0; dirIndex < 64; dirIndex++) {  // 64 × 4MB = 256MB
        pageTable = (uint32_t*)pmmAllocPage();
        memset(pageTable, 0, PAGE_SIZE);

        for (int i = 0; i < 1024; i++) {
            pageTable[i] = ((dirIndex * 1024 + i) * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
        }

        pageDir[dirIndex] = ((uint32_t)pageTable) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    __asm__ volatile ("mov %0, %%cr3" : : "r"(pageDir));
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

void pagingMapPage(uint32_t virtualAddr, uint32_t physAddr, uint32_t flags) {
    uint32_t dirIndex = PAGE_DIR_INDEX(virtualAddr);
    uint32_t tableIndex = PAGE_TABLE_INDEX(virtualAddr);

    uint32_t* pageTable;

    if (pageDir[dirIndex] & PAGE_PRESENT) {
        pageTable = (uint32_t*)(pageDir[dirIndex] & 0xFFFFF000);
    } else {
        pageTable = (uint32_t*)pmmAllocPage();
        if (!pageTable) return;
        memset(pageTable, 0, PAGE_SIZE);
        pageDir[dirIndex] = ((uint32_t)pageTable) | PAGE_PRESENT | PAGE_WRITABLE | flags;
    }

    pageTable[tableIndex] = (physAddr & 0xFFFFF000) | PAGE_PRESENT | flags;
}

void pagingUnmapPage(uint32_t virtualAddr) {
    uint32_t dirIndex = PAGE_DIR_INDEX(virtualAddr);
    uint32_t tableIndex = PAGE_TABLE_INDEX(virtualAddr);

    if (!(pageDir[dirIndex] & PAGE_PRESENT)) return;

    uint32_t* pageTable = (uint32_t*)(pageDir[dirIndex] & 0xFFFFF000);
    pageTable[tableIndex] = 0;
}