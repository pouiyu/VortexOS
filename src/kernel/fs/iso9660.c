// iso9660.c
// ISO9660 只读文件系统：解析主卷描述符(PVD)与目录记录，从光驱定位文件。
// 兼容常见变体(Rock Ridge / Joliet)，按 2048 字节逻辑块读取。
#include "iso9660.h"
#include <atapi.h>
#include <string/string.h>
#include <stdlib/stdlib.h>
#include <serial.h>
#include <stdint.h>
#include <stdbool.h>

/* 单个逻辑块缓冲 */
static uint8_t sBlock[ATAPI_BLOCK_SIZE];

static bool     sValid     = false;
static uint32_t sBlockSize = 2048;
static uint32_t sRootExt   = 0;   /* 根目录数据起始逻辑块 */
static uint32_t sRootLen   = 0;   /* 根目录数据字节数 */

/* ISO9660 目录记录：其后紧跟 nameLen 字节的文件名，再补偶字节(可选) */
typedef struct __attribute__((packed)) {
    uint8_t len;           /* 本记录长度，0 表示本块结束 */
    uint8_t extAttrLen;
    uint8_t extentLBA[8];  /* 大端 + 小端 */
    uint8_t dataLen[8];    /* 大端 + 小端 */
    uint8_t recDate[7];
    uint8_t flags;
    uint8_t unitSize;
    uint8_t gapSize;
    uint8_t volSeq[4];
    uint8_t nameLen;
    /* uint8_t name[nameLen]; */
} isoDirEntry;

/* 读小端 32 位 */
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool iso9660Init(void) {
    if (!atapiReady()) { serialPutStr("[ISO] not ready\n"); return false; }

    /* 主卷描述符(PVD)位于整个光盘的第 16 块(重定位扇区号 16) */
    if (atapiReadBlock(16, sBlock) != 0) { serialPutStr("[ISO] read PVD failed\n"); return false; }
    if (sBlock[0] != 1) { serialPutStr("[ISO] not PVD\n"); return false; }
    if (!(sBlock[1] == 'C' && sBlock[2] == 'D' && sBlock[3] == '0' &&
          sBlock[4] == '0' && sBlock[5] == '1')) { serialPutStr("[ISO] no CD001\n"); return false; }

    /* 逻辑块大小：双端序 16 位字段，小端副本在前(偏移 128-129) */
    sBlockSize = (uint32_t)sBlock[128] | ((uint32_t)sBlock[129] << 8);
    if (sBlockSize == 0) sBlockSize = 2048;

    /* 根目录记录位于 PVD 偏移 156；双端序 32 位字段小端副本在前(自身[0..3]) */
    isoDirEntry* root = (isoDirEntry*)(sBlock + 156);
    sRootExt = le32(root->extentLBA);
    sRootLen = le32(root->dataLen);

    sValid = true;
    return true;
}

/* 比较 ISO 文件名与期望名：去掉 ";版本号" 后缀、大小写不敏感 */
static bool nameEquals(const uint8_t* cdName, uint8_t nameLen, const char* want) {
    char n[64];
    uint8_t len = nameLen < 63 ? nameLen : 63;
    memcpy(n, cdName, len);
    n[len] = '\0';

    /* 去掉尾部的 ";n" 版本号 */
    if (len > 1) {
        uint8_t k = len;
        while (k > 0 && n[k - 1] >= '0' && n[k - 1] <= '9') k--;
        if (k > 0 && n[k - 1] == ';' && k < len) k--;
        if (k == 0) k = len;
        len = k;
        n[len] = '\0';
    }
    return strcasecmp(n, want) == 0;
}

/* 在某个目录数据上查找名为 name 的项 */
static bool findEntry(uint32_t ext, uint32_t dataLen, const char* name,
                      uint32_t* outExt, uint32_t* outLen, uint8_t* outFlags) {
    uint32_t blocks = dataLen / sBlockSize;
    if (dataLen % sBlockSize) blocks++;

    for (uint32_t b = 0; b < blocks; b++) {
        if (atapiReadBlock(ext + b, sBlock) != 0) return false;
        uint32_t off = 0;
        while (off + 33 <= sBlockSize) {
            uint8_t len = sBlock[off];
            if (len == 0) break;          /* 本块结束，填充到下一块 */

            isoDirEntry* e = (isoDirEntry*)(sBlock + off);
            uint8_t nlen = e->nameLen;
            const uint8_t* nm = sBlock + off + 33;

            /* 跳过 "." 与 ".." */
            if (nlen == 1 && (nm[0] == 0x00 || nm[0] == 0x01)) {
                off += len;
                continue;
            }

            if (nameEquals(nm, nlen, name)) {
                if (outExt)   *outExt = le32(e->extentLBA);
                if (outLen)   *outLen = le32(e->dataLen);
                if (outFlags) *outFlags = e->flags;
                return true;
            }
            off += len;
        }
    }
    return false;
}

/* 按路径逐段定位到某目录，返回其 ext/len；targetMustBeDir=true 时最后一段也必须是目录 */
static bool resolveDir(const char* path, bool targetMustBeDir,
                       uint32_t* outExt, uint32_t* outLen) {
    const char* p = path;
    while (*p == '/') p++;

    uint32_t curExt = sRootExt;
    uint32_t curLen = sRootLen;

    if (*p == '\0') { *outExt = curExt; *outLen = curLen; return true; }

    char seg[64];
    while (*p) {
        int i = 0;
        while (*p && *p != '/') seg[i++] = *p++;
        seg[i] = '\0';
        if (*p == '/') p++;
        if (i == 0) continue;

        uint8_t flags = 0;
        if (!findEntry(curExt, curLen, seg, &curExt, &curLen, &flags)) return false;
        if (targetMustBeDir && !(flags & 0x02)) return false;
    }

    *outExt = curExt;
    *outLen = curLen;
    return true;
}

bool iso9660FindFile(const char* path, uint32_t* ext, uint32_t* len) {
    if (!sValid) return false;
    return resolveDir(path, false, ext, len);
}

/* 去掉 ISO 文件名的 ";n" 版本号后缀，结果写入 out(供遍历回调使用) */
static void stripVersion(const uint8_t* cdName, uint8_t nameLen, char* out) {
    uint8_t len = nameLen < 63 ? nameLen : 63;
    memcpy(out, cdName, len);
    out[len] = '\0';

    if (len > 1) {
        uint8_t k = len;
        while (k > 0 && out[k - 1] >= '0' && out[k - 1] <= '9') k--;
        if (k > 0 && out[k - 1] == ';' && k < len) k--;
        if (k == 0) k = len;
        if (k < len) { out[k] = '\0'; }
    }
}

bool iso9660ListDir(const char* path, iso9660Visitor visit, void* arg) {
    if (!sValid || !visit) return false;

    uint32_t ext = 0, len = 0;
    if (!resolveDir(path, true, &ext, &len)) return false;

    uint32_t blocks = len / sBlockSize;
    if (len % sBlockSize) blocks++;
    if (blocks == 0) return true;

    /* 用独立缓冲区把整个目录一次读入，避免遍历回调里
     * 递归调用 iso9660ListDir 时再次覆盖全局 sBlock，
     * 导致外层循环读到被破坏的数据(漏掉子目录/文件)。 */
    uint8_t* buf = malloc(blocks * sBlockSize);
    if (!buf) return false;

    for (uint32_t b = 0; b < blocks; b++) {
        if (atapiReadBlock(ext + b, buf + b * sBlockSize) != 0) {
            free(buf);
            return false;
        }
    }

    for (uint32_t b = 0; b < blocks; b++) {
        uint8_t* blk = buf + b * sBlockSize;
        uint32_t off = 0;
        while (off + 33 <= sBlockSize) {
            uint8_t reclen = blk[off];
            if (reclen == 0) break;          /* 本块结束，填充到下一块 */

            isoDirEntry* e = (isoDirEntry*)(blk + off);
            uint8_t nlen = e->nameLen;
            const uint8_t* nm = blk + off + 33;
            off += reclen;

            /* 跳过 "." 与 ".." */
            if (nlen == 1 && (nm[0] == 0x00 || nm[0] == 0x01)) continue;

            char name[64];
            stripVersion(nm, nlen, name);
            if (name[0] == '\0') continue;

            if (!visit(name, le32(e->extentLBA), le32(e->dataLen), e->flags, arg)) {
                free(buf);
                return false;
            }
        }
    }
    free(buf);
    return true;
}