#include "vex.h"
#include <fs/file.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <task.h>
#include <string/string.h>
#include <stdio/vga.h>
#include <stddef.h>

static inline uint32_t alignUp(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

bool vexLoadAndRun(const char* filename) {
    FileHandle file;
    if (!fsOpen(&file, filename)) {
        vgaPutStr("VEX: open failed\n");
        return false;
    }

    VexHeader header;
    if (fsRead(&file, &header, sizeof(header)) != sizeof(header)) {
        vgaPutStr("VEX: header read failed\n");
        fsClose(&file);
        return false;
    }

    // 检查魔数和版本
    if (header.magic != VEX_MAGIC || header.version != VEX_VERSION) {
        vgaPutStr("VEX: invalid header\n");
        fsClose(&file);
        return false;
    }

    // 检查虚拟地址和大小对齐
    if ((header.codeVaddr & 0xFFF) || (header.dataVaddr & 0xFFF)) {
        vgaPutStr("VEX: vaddr must be page aligned\n");
        fsClose(&file);
        return false;
    }

    // 分配并加载代码段
    if (header.codeFileSize > 0) {
        uint32_t startPage = header.codeVaddr;
        uint32_t endPage = alignUp(header.codeVaddr + header.codeFileSize, 4096);
        uint32_t totalPages = (endPage - startPage) / 4096;

        // 为每一页分配物理内存并建立映射
        for (uint32_t page = 0; page < totalPages; page++) {
            uint32_t vaddr = startPage + page * 4096;
            uint32_t paddr = (uint32_t)pmmAllocPage();
            if (!paddr) {
                vgaPutStr("VEX: code alloc failed\n");
                fsClose(&file);
                return false;
            }
            pagingMapPage(vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }

        // 移动文件指针到代码段数据
        FileHandle codeFile;
        if (!fsOpen(&codeFile, filename)) {
            vgaPutStr("VEX: code reopen failed\n");
            fsClose(&file);
            return false;
        }

        // 跳过 header 和前面内容，直接定位到 codeFileOffset
        char temp[512];
        uint32_t toSkip = header.codeFileOffset;
        while (toSkip > 0) {
            uint32_t chunk = toSkip > sizeof(temp) ? sizeof(temp) : toSkip;
            fsRead(&codeFile, temp, chunk);
            toSkip -= chunk;
        }

        // 读取代码段到虚拟地址
        uint8_t* dest = (uint8_t*)header.codeVaddr;
        uint32_t bytesDone = 0;
        while (bytesDone < header.codeFileSize) {
            uint32_t chunk = header.codeFileSize - bytesDone;
            if (chunk > 512) chunk = 512;
            fsRead(&codeFile, dest + bytesDone, chunk);
            bytesDone += chunk;
        }
        fsClose(&codeFile);
    }

    // 加载数据段
    if (header.dataFileSize > 0) {
        uint32_t startPage = header.dataVaddr;
        uint32_t endPage = alignUp(header.dataVaddr + header.dataFileSize + header.bssSize, 4096);
        uint32_t totalPages = (endPage - startPage) / 4096;

        for (uint32_t page = 0; page < totalPages; page++) {
            uint32_t vaddr = startPage + page * 4096;
            uint32_t paddr = (uint32_t)pmmAllocPage();
            if (!paddr) {
                vgaPutStr("VEX: data alloc failed\n");
                fsClose(&file);
                return false;
            }
            pagingMapPage(vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }

        FileHandle dataFile;
        if (!fsOpen(&dataFile, filename)) {
            vgaPutStr("VEX: data reopen failed\n");
            fsClose(&file);
            return false;
        }

        char temp[512];
        uint32_t toSkip = header.dataFileOffset;
        while (toSkip > 0) {
            uint32_t chunk = toSkip > sizeof(temp) ? sizeof(temp) : toSkip;
            fsRead(&dataFile, temp, chunk);
            toSkip -= chunk;
        }

        uint8_t* dest = (uint8_t*)header.dataVaddr;
        uint32_t bytesDone = 0;
        while (bytesDone < header.dataFileSize) {
            uint32_t chunk = header.dataFileSize - bytesDone;
            if (chunk > 512) chunk = 512;
            fsRead(&dataFile, dest + bytesDone, chunk);
            bytesDone += chunk;
        }
        fsClose(&dataFile);

        // 清零 BSS
        if (header.bssSize > 0) {
            memset(dest + header.dataFileSize, 0, header.bssSize);
        }
    }

    // 创建用户任务
    uint32_t stackSize = header.userStackSize > 0 ? header.userStackSize : 4096;
    Task* task = taskCreateUser((void (*)(void))header.entryPoint, stackSize, 1);
    if (!task) {
        vgaPutStr("VEX: task create failed\n");
        fsClose(&file);
        return false;
    }

    vgaPutStr("VEX: loaded successfully\n");
    fsClose(&file);
    return true;
}