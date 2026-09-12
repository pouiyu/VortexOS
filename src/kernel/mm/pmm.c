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

    // 从 8MB 开始标记为空闲
    // [4M,8M) 保留给用户态 GUI 程序(加载地址 0x400000, 见 user.ld)，
    // 避免运行时动态分配(窗口 surface/合成缓冲)覆盖已加载的 ELF 段。
    uint32_t kernelEnd = 0x800000;  // 内核+用户程序保留区结束在 8MB
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

/* 从第一个连续空闲段里分配 count 个连续页。分配器需要连续内存(如堆大块)时使用。 */
void* pmmAllocPages(uint32_t count) {
    if (count == 0 || count > totalPages) return NULL;

    for (uint32_t page = 0; page + count <= totalPages; page++) {
        uint32_t o = BITMAP_OFFSET(page);
        if (bitmap[BITMAP_INDEX(page)] & (1 << o)) continue;  /* 起始页被占用 */

        /* 检测后续 count-1 页是否连续空闲 */
        uint32_t n = page, run = 1;
        while (run < count && page + run < totalPages) {
            uint32_t so = BITMAP_OFFSET(page + run);
            if (bitmap[BITMAP_INDEX(page + run)] & (1 << so)) break;
            run++;
        }
        if (run < count) continue;

        for (n = page; n < page + count; n++) {
            uint32_t no = BITMAP_OFFSET(n);
            bitmap[BITMAP_INDEX(n)] |= (1 << no);
            freePages--;
        }
        return (void*)(page * PAGE_SIZE);
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