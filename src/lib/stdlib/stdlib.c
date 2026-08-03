#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HEAP_START 0x1000000
#define HEAP_END   0x2000000
#define HEAP_SIZE  (HEAP_END - HEAP_START)

typedef struct Block {
    size_t size;
    int free;
    struct Block* next;
} Block;

static Block* heapStart = NULL;
static uint32_t heapTop = HEAP_START;
static size_t usedMemory = 0;

static void heapInit(void) {
    if (heapStart == NULL) {
        heapStart = (Block*)HEAP_START;
        heapStart->size = HEAP_SIZE - sizeof(Block);
        heapStart->free = 1;
        heapStart->next = NULL;
        heapTop = HEAP_START + sizeof(Block) + heapStart->size;
        usedMemory = 0;
    }
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    heapInit();
    
    size = (size + 3) & ~3;
    
    Block* current = heapStart;
    Block* bestFit = NULL;
    size_t bestSize = HEAP_SIZE;
    
    while (current) {
        if (current->free && current->size >= size) {
            if (current->size < bestSize) {
                bestFit = current;
                bestSize = current->size;
                if (bestSize == size) {
                    break;
                }
            }
        }
        current = current->next;
    }
    
    if (!bestFit) {
        return NULL;
    }
    
    if (bestFit->size > size + sizeof(Block) + 4) {
        Block* newBlock = (Block*)((uint8_t*)bestFit + sizeof(Block) + size);
        newBlock->size = bestFit->size - size - sizeof(Block);
        newBlock->free = 1;
        newBlock->next = bestFit->next;
        bestFit->next = newBlock;
        bestFit->size = size;
    }
    
    bestFit->free = 0;
    usedMemory += bestFit->size;
    
    return (void*)((uint8_t*)bestFit + sizeof(Block));
}

void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return malloc(new_size);
    }
    
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    
    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    
    if (block->next && block->next->free) {
        Block* next = block->next;
        if (block->size + sizeof(Block) + next->size >= new_size) {
            size_t totalSize = block->size + sizeof(Block) + next->size;
            block->next = next->next;
            if (totalSize > new_size + sizeof(Block)) {
                Block* newBlock = (Block*)((uint8_t*)block + sizeof(Block) + new_size);
                newBlock->size = totalSize - new_size - sizeof(Block);
                newBlock->free = 1;
                newBlock->next = block->next;
                block->next = newBlock;
                block->size = new_size;
            } else {
                block->size = totalSize;
            }
            usedMemory += block->size - new_size;
            return ptr;
        }
    }
    
    void* newPtr = malloc(new_size);
    if (!newPtr) {
        return NULL;
    }
    
    size_t copySize = (block->size < new_size) ? block->size : new_size;
    memcpy(newPtr, ptr, copySize);
    free(ptr);
    
    return newPtr;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }
    
    heapInit();
    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    block->free = 1;
    usedMemory -= block->size;
    
    Block* current = heapStart;
    while (current) {
        if (current->free && current->next && current->next->free) {
            current->size += sizeof(Block) + current->next->size;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

long atol(const char* str) {
    long result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

long long atoll(const char* str) {
    long long result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

long strtol(const char* str, char** endptr, int base) {
    long result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    if (base == 0) {
        if (*str == '0') {
            if (*(str+1) == 'x' || *(str+1) == 'X') {
                base = 16;
                str += 2;
            } else {
                base = 8;
                str++;
            }
        } else {
            base = 10;
        }
    }
    
    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') {
            digit = *str - '0';
        } else if (*str >= 'a' && *str <= 'z') {
            digit = *str - 'a' + 10;
        } else if (*str >= 'A' && *str <= 'Z') {
            digit = *str - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) {
            break;
        }
        
        result = result * base + digit;
        str++;
    }
    
    if (endptr) {
        *endptr = (char*)str;
    }
    
    return sign * result;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    return (unsigned long)strtol(str, endptr, base);
}

static unsigned int randSeed = 1;

int rand(void) {
    randSeed = randSeed * 1103515245 + 12345;
    return (unsigned int)(randSeed / 65536) % 32768;
}

void srand(unsigned int seed) {
    randSeed = seed;
}

void exit(int status) {
    (void)status;
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void abort(void) {
    while (1) {
        __asm__ volatile ("cli");
        __asm__ volatile ("hlt");
    }
}

int abs(int n) {
    return (n < 0) ? -n : n;
}

long labs(long n) {
    return (n < 0) ? -n : n;
}

long long llabs(long long n) {
    return (n < 0) ? -n : n;
}

uint32_t getHeapStart(void) {
    return HEAP_START;
}

uint32_t getHeapEnd(void) {
    return HEAP_END;
}

size_t getHeapUsed(void) {
    return usedMemory;
}

size_t getHeapFree(void) {
    heapInit();
    size_t freeMemory = 0;
    Block* current = heapStart;
    while (current) {
        if (current->free) {
            freeMemory += current->size;
        }
        current = current->next;
    }
    return freeMemory;
}

void* bsearch(const void* key, const void* base, size_t num, size_t size,
              int (*compare)(const void*, const void*)) {
    size_t left = 0;
    size_t right = num;
    
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void* item = (const void*)((const uint8_t*)base + mid * size);
        int cmp = compare(key, item);
        
        if (cmp < 0) {
            right = mid;
        } else if (cmp > 0) {
            left = mid + 1;
        } else {
            return (void*)item;
        }
    }
    
    return NULL;
}

static void swap(void* a, void* b, size_t size) {
    uint8_t temp[64];
    if (size <= 64) {
        memcpy(temp, a, size);
        memcpy(a, b, size);
        memcpy(b, temp, size);
    }
}

static void qsortRecursive(void* base, size_t left, size_t right, size_t size,
                           int (*compare)(const void*, const void*)) {
    if (left >= right) {
        return;
    }
    
    size_t pivot = left;
    size_t i = left + 1;
    size_t j = right;
    
    while (i <= j) {
        while (i <= right && compare((const uint8_t*)base + i * size,
                                     (const uint8_t*)base + pivot * size) <= 0) {
            i++;
        }
        while (j > left && compare((const uint8_t*)base + j * size,
                                   (const uint8_t*)base + pivot * size) > 0) {
            j--;
        }
        if (i < j) {
            swap((uint8_t*)base + i * size, (uint8_t*)base + j * size, size);
        }
    }
    
    swap((uint8_t*)base + pivot * size, (uint8_t*)base + j * size, size);
    
    if (j > 0) {
        qsortRecursive(base, left, j - 1, size, compare);
    }
    qsortRecursive(base, j + 1, right, size, compare);
}

void qsort(void* base, size_t num, size_t size,
           int (*compare)(const void*, const void*)) {
    if (num <= 1 || !base || !compare) {
        return;
    }
    qsortRecursive(base, 0, num - 1, size, compare);
}