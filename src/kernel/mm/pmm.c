#include "pmm.h"
#include <string/string.h>

static uint32_t totalPages = 0;
static uint32_t freePages = 0;
static uint32_t bitmapStart = 0;
static uint32_t bitmapSize = 0;
static uint8_t* bitmap = NULL;

#define BITMAP_INDEX(page)  ((page) / 8)
#define BITMAP_OFFSET(page) ((page) % 8)

void pmmInit(uint32_t totalMemory) {
    totalPages = totalMemory / PAGE_SIZE;

    // 位图放在内核之后（1MB 加载，内核大约 2MB）
    bitmapStart = 0x200000;  // 2MB 处放位图
    bitmapSize = (totalPages + 7) / 8;
    bitmap = (uint8_t*)bitmapStart;

    // 全部标记为已使用
    memset(bitmap, 0xFF, bitmapSize);

    // 从 4MB 开始标记为空闲
    uint32_t kernelEnd = 0x400000;  // 内核结束在 4MB
    uint32_t startPage = kernelEnd / PAGE_SIZE;

    for (uint32_t page = startPage; page < totalPages; page++) {
        bitmap[BITMAP_INDEX(page)] &= ~(1 << BITMAP_OFFSET(page));
        freePages++;
    }
}

void* pmmAllocPage(void) {
    for (uint32_t page = 0; page < totalPages; page++) {
        uint8_t offset = BITMAP_OFFSET(page);
        if (!(bitmap[BITMAP_INDEX(page)] & (1 << offset))) {
            bitmap[BITMAP_INDEX(page)] |= (1 << offset);
            freePages--;
            return (void*)(page * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmmFreePage(void* pageAddr) {
    uint32_t page = (uint32_t)pageAddr / PAGE_SIZE;
    if (page >= totalPages) return;

    bitmap[BITMAP_INDEX(page)] &= ~(1 << BITMAP_OFFSET(page));
    freePages++;
}

uint32_t pmmGetFreePages(void) {
    return freePages;
}

uint32_t pmmGetTotalPages(void) {
    return totalPages;
}