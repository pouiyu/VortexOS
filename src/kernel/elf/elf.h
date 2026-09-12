#ifndef _KERNEL_ELF_ELF_H
#define _KERNEL_ELF_ELF_H

#include <stdint.h>
#include <stdbool.h>

// ELF 头
typedef struct {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) Elf32Header;

// 程序头
typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed)) Elf32ProgramHeader;

#define ELF_PT_LOAD  1
#define ELF_EM_386   3
#define ELF_ET_EXEC  2

// 加载 ELF 文件并创建用户任务
bool elfLoadAndRun(const char* filename);

// 仅加载 ELF 段到内存(身份映射, 段写入其 vaddr), 返回入口地址; 失败返回 0。
// 用于用户 GUI 程序: 加载后经 jumpToUserGui 直接跳转执行, 不建任务。
uint32_t elfLoad(const char* filename);

#endif