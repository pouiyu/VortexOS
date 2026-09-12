#ifndef _KERNEL_FS_FILE_H
#define _KERNEL_FS_FILE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t cluster;      // 起始簇
    uint32_t size;         // 文件大小
    uint32_t position;     // 当前读写位置
    bool isDirectory;
    const uint8_t* modData; // 非空 = GRUB 模块后备(U盘启动无 CD/未格式化盘时用)
} FileHandle;

bool fsOpen(FileHandle* file, const char* path);
int  fsRead(FileHandle* file, void* buf, uint32_t count);
void fsClose(FileHandle* file);

/* 解析 Multiboot2 信息中的模块(tag type 3)注册为只读文件后备。
 * 模块 cmdline 即其虚拟路径(如 /system/font/font.bin)，匹配不区分大小写。 */
void fsRegisterModules(uint32_t mb2InfoAddr);

/* 按路径查模块数据(供安装器在无 CD 时从模块拷贝 system 树)；找到返回大小 */
const uint8_t* fsFindModule(const char* path, uint32_t* outSize);

#endif