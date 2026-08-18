#include "stdlib.h"
#include <string/string.h>
#include <stdio/stdio.h>
#include <mm/pmm.h>

typedef struct Block {
    size_t size;
    int free;
    struct Block* next;
} Block;

static Block* heapStart = NULL;
static size_t usedMemory = 0;

// 从 PMM 分配新页给堆
static Block* heapGrow(void) {
    void* page = pmmAllocPage();
    if (!page) return NULL;

    Block* block = (Block*)page;
    block->size = PAGE_SIZE - sizeof(Block);
    block->free = 1;
    block->next = NULL;

    // 加入堆链表
    if (!heapStart) {
        heapStart = block;
    } else {
        Block* current = heapStart;
        while (current->next) {
            current = current->next;
        }
        current->next = block;
    }

    return block;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;

    size = (size + 3) & ~3;

    // 首次适应：找空闲块
    Block* current = heapStart;
    while (current) {
        if (current->free && current->size >= size) {
            // 拆分
            if (current->size > size + sizeof(Block) + 4) {
                Block* newBlock = (Block*)((uint8_t*)current + sizeof(Block) + size);
                newBlock->size = current->size - size - sizeof(Block);
                newBlock->free = 1;
                newBlock->next = current->next;
                current->next = newBlock;
                current->size = size;
            }

            current->free = 0;
            usedMemory += current->size;
            return (void*)((uint8_t*)current + sizeof(Block));
        }
        current = current->next;
    }

    // 没有合适的块，扩展堆
    Block* newBlock = heapGrow();
    if (!newBlock) return NULL;

    return malloc(size);  // 重新分配
}

void free(void* ptr) {
    if (!ptr) return;

    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    block->free = 1;
    usedMemory -= block->size;

    // 合并相邻空闲块
    Block* current = heapStart;
    while (current) {
        if (current->free && current->next && current->next->free) {
            current->size += sizeof(Block) + current->next->size;
            current->next = current->next->next;
        }
        current = current->next;
    }
}