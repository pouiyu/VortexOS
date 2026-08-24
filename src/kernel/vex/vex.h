#ifndef _KERNEL_VEX_VEX_H
#define _KERNEL_VEX_VEX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entryPoint;
    uint32_t codeFileOffset;
    uint32_t codeFileSize;
    uint32_t codeVaddr;
    uint32_t dataFileOffset;
    uint32_t dataFileSize;
    uint32_t dataVaddr;
    uint32_t bssSize;
    uint32_t userStackSize;
    uint32_t flags;
} __attribute__((packed)) VexHeader;

#define VEX_MAGIC 0x00000056
#define VEX_VERSION 1

// 加载并运行 VEX 文件
bool vexLoadAndRun(const char* filename);

#endif