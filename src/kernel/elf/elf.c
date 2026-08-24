#include "elf.h"
#include <fs/file.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <task.h>
#include <string/string.h>
#include <stdlib/stdlib.h>
#include <vga.h>

bool elfLoadAndRun(const char* filename) {
    FileHandle file;
    if (!fsOpen(&file, filename)) {
        vgaPutStr("ELF: open failed\n");
        return false;
    }

    // 读取 ELF 头
    Elf32Header header;
    if (fsRead(&file, &header, sizeof(header)) != sizeof(header)) {
        vgaPutStr("ELF: header read failed\n");
        fsClose(&file);
        return false;
    }

    // 检查魔数
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L'  || header.ident[3] != 'F') {
        vgaPutStr("ELF: bad magic\n");
        fsClose(&file);
        return false;
    }

    // 检查架构与类型
    if (header.machine != ELF_EM_386 || header.type != ELF_ET_EXEC) {
        vgaPutStr("ELF: unsupported arch/type\n");
        fsClose(&file);
        return false;
    }

    // 读取程序头表
    uint32_t phoff = header.phoff;
    uint16_t phnum = header.phnum;
    uint16_t phentsize = header.phentsize;

    if (phnum == 0 || phentsize != sizeof(Elf32ProgramHeader)) {
        vgaPutStr("ELF: invalid phdr\n");
        fsClose(&file);
        return false;
    }

    // 分配程序头缓冲区
    Elf32ProgramHeader* phdrs = (Elf32ProgramHeader*)malloc(phnum * sizeof(Elf32ProgramHeader));
    if (!phdrs) {
        vgaPutStr("ELF: no memory for phdrs\n");
        fsClose(&file);
        return false;
    }

    // 移动文件指针到程序头（fsRead 当前位于 header 之后）
    char temp[512];
    uint32_t toSkip = phoff - sizeof(header);
    while (toSkip > 0) {
        uint32_t chunk = toSkip > sizeof(temp) ? sizeof(temp) : toSkip;
        fsRead(&file, temp, chunk);
        toSkip -= chunk;
    }

    // 读取所有程序头
    uint32_t phdrsSize = phnum * sizeof(Elf32ProgramHeader);
    if (fsRead(&file, phdrs, phdrsSize) != (int)phdrsSize) {
        vgaPutStr("ELF: phdr read failed\n");
        free(phdrs);
        fsClose(&file);
        return false;
    }

    // 遍历加载段
    for (int i = 0; i < phnum; i++) {
        if (phdrs[i].type != ELF_PT_LOAD) continue;

        uint32_t vaddr = phdrs[i].vaddr;
        uint32_t filesz = phdrs[i].filesz;
        uint32_t memsz = phdrs[i].memsz;
        uint32_t offset = phdrs[i].offset;

        if (filesz > memsz) {
            vgaPutStr("ELF: invalid segment sizes\n");
            free(phdrs);
            fsClose(&file);
            return false;
        }

        // 确保段所在页已映射（身份映射下无需额外操作，但需写入）
        // 这里假设身份映射且物理地址=虚拟地址，段数据写入精确的 vaddr
        uint8_t* dest = (uint8_t*)vaddr;

        // 重新打开文件读取段数据（简化，避免文件指针管理）
        FileHandle segFile;
        if (!fsOpen(&segFile, filename)) {
            vgaPutStr("ELF: reopen failed\n");
            free(phdrs);
            fsClose(&file);
            return false;
        }

        // 跳过 offset 字节
        uint32_t toRead = offset;
        while (toRead > 0) {
            uint32_t chunk = toRead > sizeof(temp) ? sizeof(temp) : toRead;
            fsRead(&segFile, temp, chunk);
            toRead -= chunk;
        }

        // 读取段数据到目标地址
        uint32_t bytesDone = 0;
        while (bytesDone < filesz) {
            uint32_t chunk = filesz - bytesDone;
            if (chunk > 512) chunk = 512;
            fsRead(&segFile, dest + bytesDone, chunk);
            bytesDone += chunk;
        }

        // 清零 BSS
        if (memsz > filesz) {
            memset(dest + filesz, 0, memsz - filesz);
        }

        fsClose(&segFile);
    }

    // 入口点
    uint32_t entry = header.entry;

    // 创建用户任务
    Task* task = taskCreateUser((void (*)(void))entry, 4096, 1);
    if (!task) {
        vgaPutStr("ELF: task create failed\n");
        free(phdrs);
        fsClose(&file);
        return false;
    }

    vgaPutStr("ELF: loaded successfully\n");
    free(phdrs);
    fsClose(&file);
    return true;
}