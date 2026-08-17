#ifndef _KERNEL_FS_FILE_H
#define _KERNEL_FS_FILE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t cluster;      // 起始簇
    uint32_t size;         // 文件大小
    uint32_t position;     // 当前读写位置
    bool isDirectory;
} FileHandle;

bool fsOpen(FileHandle* file, const char* path);
int  fsRead(FileHandle* file, void* buf, uint32_t count);
void fsClose(FileHandle* file);

#endif