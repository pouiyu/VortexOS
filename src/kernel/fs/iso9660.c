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
static bool     sJoliet    = false;   /* 是否使用 Joliet(UTF-16BE 长名)补充卷描述符 */
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

    uint32_t pvdExt = 0, pvdLen = 0;       /* 普通 ISO9660(PVD) */
    uint32_t jolExt = 0, jolLen = 0;       /* Joliet(SVD, UTF-16BE 长名) */
    bool havePvd = false, haveJoliet = false;

    /* 卷描述符序列从逻辑块 16 开始，16~31 内逐个扫描；
     * 类型 1=PVD，类型 2=SVD(可能为 Joliet)，类型 255=终结符。 */
    for (uint32_t lba = 16; lba < 32; lba++) {
        if (atapiReadBlock(lba, sBlock) != 0) break;
        uint8_t type = sBlock[0];
        if (type == 255) break;
        if (type != 1 && type != 2) continue;
        if (!(sBlock[1] == 'C' && sBlock[2] == 'D' && sBlock[3] == '0' &&
              sBlock[4] == '0' && sBlock[5] == '1')) continue;

        /* 逻辑块大小(双端序 16 位，PVD 在小端副本偏移 128-129) */
        uint32_t bs = (uint32_t)sBlock[128] | ((uint32_t)sBlock[129] << 8);
        if (bs != 0) sBlockSize = bs;
        else if (sBlockSize != 2048 && sBlockSize != 0) sBlockSize = 2048;

        /* 根目录记录位于卷描述符偏移 156 */
        isoDirEntry* root = (isoDirEntry*)(sBlock + 156);
        if (type == 1) {
            pvdExt = le32(root->extentLBA);
            pvdLen = le32(root->dataLen);
            havePvd = true;
        } else {
            /* Joliet SVD 的 Escape Sequence(偏移 88, 32 字节)以 "%/" 开头，
             * 第三字节为 @ / C / E / 1 / 2 之一才表明目录名是 UTF-16BE。 */
            const uint8_t* esc = sBlock + 88;
            if (esc[0] == '%' && esc[1] == '/' &&
                (esc[2] == '@' || esc[2] == 'C' || esc[2] == 'E' ||
                 esc[2] == '1' || esc[2] == '2')) {
                jolExt = le32(root->extentLBA);
                jolLen = le32(root->dataLen);
                haveJoliet = true;
                break;   /* 只读 16~18，够用；拿到即止减少 CD 读 */
            }
        }
    }

    if (!havePvd && !haveJoliet) { serialPutStr("[ISO] no volume descriptor\n"); return false; }

    /* 优先使用 Joliet：装盘时能拿到完整长名，避免 8.3 截断碰撞 */
    sJoliet = haveJoliet;
    if (sJoliet) { sRootExt = jolExt; sRootLen = jolLen; }
    else         { sRootExt = pvdExt; sRootLen = pvdLen; }

    serialPutStr(sJoliet ? "[ISO] joliet root\n" : "[ISO] iso9660 root\n");
    sValid = true;
    return true;
}

/* 把目录记录的文件名字字段解码成 ASCII 字符串(写 NUL 结尾)：
 *   - Joliet：UTF-16BE，每字符 2 字节，无 ";n" 版本号；
 *     可读 ASCII 直接保留，非 ASCII 一律映射为 '?'；
 *   - 普通 ISO：ASCII(8.3)，结尾可能带 ";n" 版本号，需要去掉。 */
static void isoDirName(const uint8_t* nm, uint8_t byteLen, char* out) {
    if (sJoliet) {
        int idx = 0;
        uint8_t i = 0;
        while (i + 1 < byteLen && idx < 63) {
            uint16_t u = ((uint16_t)nm[i] << 8) | nm[i + 1];
            out[idx++] = (u < 0x80) ? (char)u : '?';
            i += 2;
        }
        out[idx] = '\0';
    } else {
        uint8_t len = byteLen < 63 ? byteLen : 63;
        memcpy(out, nm, len);
        out[len] = '\0';
        /* 去掉尾部的 ";n" 版本号 */
        if (len > 1) {
            uint8_t k = len;
            while (k > 0 && out[k - 1] >= '0' && out[k - 1] <= '9') k--;
            if (k > 0 && out[k - 1] == ';' && k < len) k--;
            if (k == 0) k = len;
            out[k] = '\0';
        }
    }
}

/* 是否为 "." / ".." 目录项(普通 ISO 用 0x00/0x01 表示，Joliet 用真实点号) */
static bool isDotDir(const char* n) {
    if (n[0] == '\0' || n[0] == '\x01') return true;
    return strcmp(n, ".") == 0 || strcmp(n, "..") == 0;
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

            /* 记录名解码后判断是否为 "." 或 ".." */
            char nbuf[64];
            isoDirName(nm, nlen, nbuf);
            if (isDotDir(nbuf)) {
                off += len;
                continue;
            }

            if (strcasecmp(nbuf, name) == 0) {
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

/* 去掉 ISO 文件名的 ";n" 版本号后缀，结果写入 out(供遍历回调使用)。
 * Joliet(长名)模式下名称无版本号，这里直接走统一解码。 */
static void stripVersion(const uint8_t* cdName, uint8_t nameLen, char* out) {
    isoDirName(cdName, nameLen, out);
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

            char name[64];
            stripVersion(nm, nlen, name);
            if (isDotDir(name)) continue;

            if (!visit(name, le32(e->extentLBA), le32(e->dataLen), e->flags, arg)) {
                free(buf);
                return false;
            }
        }
    }
    free(buf);
    return true;
}