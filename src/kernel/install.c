// install.c
// 系统安装与字体自动加载：
//   1) FAT32 初始化失败时，格式化硬盘并创建文件系统；
//   2) 从光驱(ISO9660)读取字体文件；
//   3) 写入硬盘的 /system/font 目录；
//   4) 开机时若字体尚未安装则自动加载，并读回验证。
#include "install.h"
#include <fs/fat32.h>
#include <fs/iso9660.h>
#include <fs/file.h>
#include <atapi.h>
#include <ata.h>
#include <stdio/vga.h>
#include <stdio/vbe.h>
#include <serial.h>
#include <string/string.h>
#include <stdlib/stdlib.h>
#include <kernel.h>
#include <keyboard.h>
#include "device.h"

static void instLog(const char* s) {
    serialPutStr(s);
}

/* 输出带前缀的日志行 */
static void instLogLine(const char* s) {
    serialPutStr(s);
    serialPutStr("\n");
    vgaPutStr(s);
    vgaPutChar('\n');
}

/* 打印一个 32 位十进制数到串口(用于读回验证) */
static void putDec32(uint32_t value) {
    char tmp[12];
    int i = 0;
    if (value == 0) { serialPutStr("0"); return; }
    while (value) { tmp[i++] = (char)('0' + value % 10); value /= 10; }
    while (i--) serialPutStr((char[]){ tmp[i], 0 });   // 逐字符输出
}

/* 前向声明(定义在 installSystem 之后) */
static bool installWriteGrub(void);
static bool installBootFiles(void);

/* 从光驱读取整个文件到动态内存，返回缓冲区与字节数 */
static uint8_t* readCdFile(const char* path, uint32_t* outSize) {
    if (!iso9660Init()) { instLogLine("[FONT] ISO9660 init failed"); return 0; }

    uint32_t ext = 0, len = 0;
    if (!iso9660FindFile(path, &ext, &len)) { instLogLine("[FONT] not found on CD"); return 0; }
    if (len == 0) { instLogLine("[FONT] empty file on CD"); return 0; }

    uint32_t blocks = len / ATAPI_BLOCK_SIZE;
    if (len % ATAPI_BLOCK_SIZE) blocks++;

    /* 按“块数”分配：atapiReadBlock 每次都写满 2048 字节，可能超过 len 对齐 */
    uint32_t allocSize = blocks * ATAPI_BLOCK_SIZE;
    uint8_t* buf = malloc(allocSize);
    if (!buf) { instLogLine("[FONT] out of memory"); return 0; }

    for (uint32_t i = 0; i < blocks; i++) {
        if (atapiReadBlock(ext + i, buf + i * ATAPI_BLOCK_SIZE) != 0) {
            instLogLine("[FONT] CD read failed");
            free(buf);
            return 0;
        }
    }

    if (outSize) *outSize = len;
    return buf;
}

/* 安装系统：格式化硬盘 + 装 GRUB 引导 + 拷贝系统文件与字体 */
bool installSystem(void) {
    instLogLine("[INSTALL] Formatting disk (FAT32)...");
    if (!fat32Format(&fsVolume)) { instLogLine("[INSTALL] Format failed"); return false; }

    /* 写完 FAT32 后再把 GRUB 引导代码覆盖到扇区 0(分区表一致，不破坏文件系统) */
    if (!installWriteGrub()) { return false; }

    if (!fat32Init(&fsVolume))   { instLogLine("[INSTALL] Re-init failed"); return false; }

    if (!installBootFiles()) { return false; }

    instLogLine("[INSTALL] System installed");
    return true;
}

/* 把 GRUB boot.img+分区表写入扇区 0，core.img 写入扇区 1 起的 gap */
static bool installWriteGrub(void) {
    uint32_t sz = 0;
    uint8_t* mbr = readCdFile(CD_GRUB_MBR_PATH, &sz);
    if (!mbr) { instLogLine("[GRUB] hdd_mbr.bin not on CD"); return false; }
    if (sz != 512) { instLogLine("[GRUB] bad MBR size"); free(mbr); return false; }
    if (ataWriteSector(0, mbr) != 0) { instLogLine("[GRUB] write MBR failed"); free(mbr); return false; }
    free(mbr);

    uint8_t* core = readCdFile(CD_GRUB_CORE_PATH, &sz);
    if (!core) { instLogLine("[GRUB] core.img not on CD"); return false; }
    uint32_t n = (sz + 511) / 512;
    for (uint32_t i = 0; i < n; i++) {
        if (ataWriteSector(GRUB_SECTOR_START + i, core + (i * 512)) != 0) {
            instLogLine("[GRUB] write core.img failed");
            free(core);
            return false;
        }
    }
    free(core);
    instLog("[GRUB] written MBR+core.img, ");
    putDec32(n);
    instLogLine(" sectors");
    return true;
}

/* 一键落盘：在指定目录下创建并写入一个文件 */
static bool writeDiskFile(const char* dir, const char* path,
                          const uint8_t* data, uint32_t size) {
    if (!fat32MkDirs(&fsVolume, dir)) { instLogLine("[INSTALL] mkdir failed"); return false; }
    if (!fat32FileExists(&fsVolume, path)) {
        if (!fat32CreateEntry(&fsVolume, path, false)) {
            instLogLine("[INSTALL] create file failed");
            return false;
        }
    }
    if (!fat32WriteRawFile(&fsVolume, path, data, size)) {
        instLogLine("[INSTALL] write file failed");
        return false;
    }
    return true;
}

/* 从光驱递归拷整个 /system 运行目录到硬盘 */
typedef struct {
    const char* cdDir;   /* 当前 CD 目录，如 "SYSTEM" */
    const char* dsDir;   /* 当前磁盘目录，如 "/system" */
    bool ok;             /* 是否仍可继续 */
} SysCopyCtx;

/* base + "/" + name */
static void pathJoin(char* out, const char* base, const char* name) {
    strcpy(out, base);
    size_t n = strlen(out);
    if (n > 0 && out[n - 1] != '/') strcat(out, "/");
    strcat(out, name);
}

/* 转小写(ISO9660 条目名是大写，磁盘用全小写路径) */
static void lowerName(const char* src, char* out) {
    int i = 0;
    for (i = 0; src[i] && i < 63; i++) {
        char c = src[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[i] = '\0';
}

/* 安装进度：逐项打印到屏幕与串口，供用户看到正在安装的文件/文件夹 */
static void instProgress(const char* tag, const char* name) {
    serialPutStr(tag);
    serialPutStr(name);
    serialPutStr("\n");
    vgaPutStr(tag);
    vgaPutStr(name);
    vgaPutChar('\n');
}

static bool sysCopyEntry(const char* name, uint32_t ext, uint32_t len,
                         uint8_t flags, void* arg) {
    (void)ext; (void)len;
    SysCopyCtx* ctx = (SysCopyCtx*)arg;

    char low[64]; lowerName(name, low);
    char cdPath[96]; pathJoin(cdPath, ctx->cdDir, name);
    char dsPath[96]; pathJoin(dsPath, ctx->dsDir, low);

    if (flags & 0x02) {
        /* 子目录：建目录后递归 */
        if (!fat32MkDirs(&fsVolume, dsPath)) { ctx->ok = false; return false; }
        instProgress("[DIR ] ", low);
        SysCopyCtx sub = { cdPath, dsPath, true };
        if (!iso9660ListDir(cdPath, sysCopyEntry, &sub) || !sub.ok) {
            ctx->ok = false;
            return false;
        }
        return true;
    }

    uint32_t sz = 0;
    uint8_t* data = readCdFile(cdPath, &sz);
    if (!data) { ctx->ok = false; return false; }
    bool w = writeDiskFile(ctx->dsDir, dsPath, data, sz);
    free(data);
    if (!w) { ctx->ok = false; return false; }
    instProgress("[FILE] ", low);
    return true;
}

/* 清空硬盘 /system 下的所有条目(实现"完整替换"：删除 CD 上已不存在的旧文件) */
static void clearSystemDir(void) {
    if (!fsVolume.valid) return;
    static char list[2048];
    if (!fat32ListDir(&fsVolume, DISK_SYSTEM_DIR, list, sizeof(list))) return;
    char* line = list;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (!nl) break;
        *nl = '\0';
        /* 每行格式 "<D> 名" 或 "<F> 名" */
        if (line[0] == '<') {
            const char* name = line + 4;
            if (name[0] != '\0' && name[0] != '.') {
                char full[128];
                pathJoin(full, DISK_SYSTEM_DIR, name);
                serialPutStr("[DEL ] "); serialPutStr(name); serialPutStr("\n");
                vgaPutStr("[DEL ] "); vgaPutStr(name); vgaPutChar('\n');
                fat32Remove(&fsVolume, full);
            }
        }
        line = nl + 1;
    }
}

/* 把光驱 SYSTEM 目录整树拷贝到硬盘 /system(含字体)，
 * 拷贝前先清空硬盘 /system -> 完整替换 CD 系统文件夹。 */
static bool installCopySystem(void) {
    if (!iso9660Init()) { instLogLine("[INSTALL] ISO9660 init failed"); return false; }

    clearSystemDir();

    SysCopyCtx ctx = { CD_SYSTEM_DIR, DISK_SYSTEM_DIR, true };
    if (!iso9660ListDir(CD_SYSTEM_DIR, sysCopyEntry, &ctx) || !ctx.ok) {
        instLogLine("[INSTALL] copy SYSTEM failed");
        return false;
    }
    instLogLine("[INSTALL] /system copied from CD");
    return true;
}

/* 从光驱拷贝内核、grub.cfg、并把整个 system 运行目录(含字体)拷到硬盘 */
static bool installBootFiles(void) {
    uint32_t sz = 0;

    uint8_t* k = readCdFile(CD_KERNEL_PATH, &sz);
    if (!k) { instLogLine("[INSTALL] kernel.bin not on CD"); return false; }
    bool ok = writeDiskFile(DISK_BOOT_DIR, DISK_KERNEL, k, sz);
    free(k);
    if (!ok) return false;

    uint8_t* cfg = readCdFile(CD_GRUB_CFG_PATH, &sz);
    if (!cfg) { instLogLine("[INSTALL] grub.cfg not on CD"); return false; }
    ok = writeDiskFile(DISK_GRUB_DIR, DISK_GRUB_CFG, cfg, sz);
    free(cfg);
    if (!ok) return false;

    instLogLine("[INSTALL] kernel+grub.cfg copied");
    return installCopySystem();
}

/* 从光驱把字体写入硬盘 /system/font */
bool loadFontFromCd(void) {
    uint32_t size = 0;
    uint8_t* data = readCdFile(CD_FONT_PATH, &size);
    if (!data) return false;

    bool ok = writeDiskFile(DISK_FONT_DIR, DISK_FONT_PATH, data, size);
    free(data);
    if (!ok) return false;

    instLog("[FONT] installed to /system/font, size=");
    putDec32(size);
    instLogLine(" bytes");

    /* 顺带从光驱刷新中文 16x16 字库到硬盘（随 system 整树分发） */
    uint32_t cjkSize = 0;
    uint8_t* cjk = readCdFile(CD_FONT_CJK_PATH, &cjkSize);
    if (cjk) {
        if (cjkSize >= 36)
            writeDiskFile(DISK_FONT_DIR, DISK_FONT_CJK_PATH, cjk, cjkSize);
        free(cjk);
    }
    return true;
}

/* 读回验证：打开硬盘上的字体，打印大小 */
void verifyFontOnDisk(void) {
    if (!fsVolume.valid) return;

    FileHandle f;
    if (!fsOpen(&f, DISK_FONT_PATH)) {
        instLogLine("[FONT] not present on disk");
        return;
    }
    uint32_t size = f.size;
    fsClose(&f);

    instLog("[FONT] read back from disk: ");
    putDec32(size);
    instLogLine(" bytes");
}

/* 把硬盘上的 font.bin 读进内存并注册给 VBE，供图形模式 8x16 渲染使用。 */
void loadFontIntoVbe(void) {
    if (!fsVolume.valid) return;

    FileHandle f;
    if (!fsOpen(&f, DISK_FONT_PATH)) return;
    if (f.size <= 0 || f.size > 256 * 17) { fsClose(&f); return; }

    uint8_t* buf = malloc(f.size);
    if (!buf) { fsClose(&f); return; }

    int got = fsRead(&f, buf, f.size);
    fsClose(&f);

    if (got > 0) vbeLoadFont(buf, (uint32_t)got);
    free(buf);

    instLog("[FONT] loaded into VBE: ");
    putDec32((uint32_t)got);
    instLogLine(" bytes");
}

/* 把硬盘上的 cjk16.bin 读进内存并注册给 VBE，供图形模式 16x16 中文渲染使用。 */
void loadCjkFontIntoVbe(void) {
    if (!fsVolume.valid) return;

    FileHandle f;
    if (!fsOpen(&f, DISK_FONT_CJK_PATH)) return;
    if (f.size < 36) { fsClose(&f); return; }

    uint8_t* buf = malloc(f.size);
    if (!buf) { fsClose(&f); return; }

    int got = fsRead(&f, buf, f.size);
    fsClose(&f);

    if (got >= 36) vbeLoadCjkFont(buf, (uint32_t)got);
    free(buf);

    instLog("[FONT] CJK loaded into VBE: ");
    putDec32((uint32_t)got);
    instLogLine(" bytes");
}

/* ==================== 文本模式安装向导 ==================== */

static void instClear(void);
static bool cdSystemDiffers(void);

/* 底部固定选项行：Yes = Y/y  No = N/n  Back = Backspace */
static void instOptionBar(void) {
    vgaSetCursorPos(VGA_HEIGHT - 1, 0);
    vgaSetColorByte(theme);
    for (int i = 0; i < VGA_WIDTH; i++) vgaPutChar(' ');
    vgaSetCursorPos(VGA_HEIGHT - 1, 0);
    vgaPutStr("Yes = Y/y    No = N/n    Back = Backspace");
}

/* 绘制一页：顶部标题、中央问题、底部选项行(无过多装饰) */
static void instDrawPage(const char* title, const char* const* lines, int nLines) {
    vgaDisableCursor();
    vgaSetColorByte(theme);
    vgaClear();

    /* 串口镜像打印，便于无头验证当前页面 */
    serialPutStr("\n[WIZARD] ");
    serialPutStr(title);
    serialPutStr("\n");
    for (int i = 0; i < nLines; i++) {
        serialPutStr("  ");
        serialPutStr(lines[i]);
        serialPutStr("\n");
    }

    vgaSetCursorPos(0, 0);
    vgaPutChar(' ');
    vgaPutStr(title);
    vgaPutStr(" \n\n");

    int startRow = 10;
    for (int i = 0; i < nLines; i++) {
        int len = strlen(lines[i]);
        int col = (VGA_WIDTH - len) / 2;
        if (col < 0) col = 0;
        vgaSetCursorPos((uint8_t)(startRow + i), (uint8_t)col);
        vgaPutStr(lines[i]);
    }
    instOptionBar();
}

typedef enum { INST_CHOICE_NONE, INST_CHOICE_YES, INST_CHOICE_NO, INST_CHOICE_BACK } InstChoice;

static InstChoice instAsk(const char* title, const char* const* lines, int nLines) {
    instDrawPage(title, lines, nLines);
    for (;;) {
        if (keyboardHasChar()) {
            unsigned char c = keyboardGetChar();
            if (c == 'y' || c == 'Y') return INST_CHOICE_YES;
            if (c == 'n' || c == 'N') return INST_CHOICE_NO;
            if (c == '\b') return INST_CHOICE_BACK;
        }
        __asm__ volatile ("hlt");
    }
}

/* 安装完成提示并短暂停顿后重启 */
static void instRebootSoon(void) {
    instClear();
    vgaSetCursorPos(11, 0);
    int len = strlen("Installation complete. Rebooting...");
    vgaSetCursorPos(11, (uint8_t)((VGA_WIDTH - len) / 2));
    vgaPutStr("Installation complete. Rebooting...");
    for (volatile int i = 0; i < 20000000; i++) __asm__ volatile ("pause");
    deviceReboot();
}

static void instClear(void) {
    vgaDisableCursor();
    vgaSetColorByte(theme);
    vgaClear();
}

/* 全新安装：格式化硬盘 + 装引导 + 整树拷贝 /system */
static bool installFresh(void) {
    instClear();
    vgaSetCursorPos(2, 0);
    vgaPutStr("Formatting and installing VortexOS...\n\n");
    return installSystem();
}

/* 更新：清空 /system 后整树重拷(不重新分区/格式化) */
static bool installUpdate(void) {
    instClear();
    vgaSetCursorPos(2, 0);
    vgaPutStr("Updating system from CD-ROM...\n\n");
    return installCopySystem();
}

/* 硬盘未格式化分支：循环询问“是否格式化安装” / “是否从 CD 运行” */
static InstallResult installWizardNotFormatted(void) {
    static const char* const fmtLines[] = {
        "Hard disk is not formatted.",
        "",
        "Format the disk and install",
        "VortexOS to it?",
    };
    static const char* const cdLines[] = {
        "Hard disk is not formatted.",
        "",
        "Run VortexOS from the CD-ROM",
        "(no changes to the disk)?",
    };

    for (;;) {
        InstChoice c = instAsk("VortexOS Installer", fmtLines, 4);
        if (c == INST_CHOICE_YES) {
            if (installFresh()) { instRebootSoon(); }
            continue;              /* 安装失败则重新询问 */
        }
        if (c == INST_CHOICE_NO) {
            InstChoice cc = instAsk("VortexOS Installer", cdLines, 4);
            if (cc == INST_CHOICE_YES) return INST_RESULT_RUN_CD;
            continue;              /* No / Back -> 回到"是否格式化" */
        }
        /* Back：未格式化时上一步就是开始，原地等待 */
    }
}

/* 硬盘已格式化且 CD 系统不同：询问是否更新 */
static InstallResult installWizardUpdate(void) {
    static const char* const upLines[] = {
        "The system on the CD-ROM differs",
        "from the one on the hard disk.",
        "",
        "Update the system on the hard disk?",
    };
    for (;;) {
        InstChoice c = instAsk("VortexOS Installer", upLines, 4);
        if (c == INST_CHOICE_YES) {
            if (installUpdate()) { instRebootSoon(); }
            return INST_RESULT_BOOT; /* 更新失败则继续当前启动 */
        }
        return INST_RESULT_BOOT;     /* No / Back -> 继续启动 */
    }
}

/* 交互式安装向导入口：
 *   硬盘未格式化 -> 询问格式化安装 / 从 CD 运行
 *   已格式化且 CD 系统不同 -> 询问更新
 *   已格式化且一致 -> 直接返回继续启动 */
InstallResult installWizard(void) {
    if (!fsVolume.valid) {
        return installWizardNotFormatted();
    }
    if (!cdSystemDiffers()) {
        return INST_RESULT_BOOT;
    }
    return installWizardUpdate();
}

/* 比较 CD SYSTEM 与硬盘 /system：任一文件/目录缺失或大小不同即为不同 */
typedef struct {
    const char* cdDir;
    const char* dsDir;
    bool differs;
} DiffCtx;

static bool diffEntry(const char* name, uint32_t ext, uint32_t len,
                      uint8_t flags, void* arg) {
    (void)ext;
    DiffCtx* ctx = (DiffCtx*)arg;
    char low[64]; lowerName(name, low);
    char cdPath[128]; pathJoin(cdPath, ctx->cdDir, name);
    char dsPath[128]; pathJoin(dsPath, ctx->dsDir, low);

    if (flags & 0x02) {
        /* 子目录：硬盘侧必须存在同名字目录，否则不同 */
        if (!fat32FileExists(&fsVolume, dsPath)) { ctx->differs = true; return true; }
        DiffCtx sub = { cdPath, dsPath, ctx->differs };
        iso9660ListDir(cdPath, diffEntry, &sub);
        ctx->differs = sub.differs;
        return true;
    }

    if (!fat32FileExists(&fsVolume, dsPath)) { ctx->differs = true; return true; }
    FileHandle f;
    if (!fsOpen(&f, dsPath)) { ctx->differs = true; return true; }
    if (f.size != len) { fsClose(&f); ctx->differs = true; return true; }
    fsClose(&f);
    return true;
}

static bool cdSystemDiffers(void) {
    if (!iso9660Init()) return true;
    DiffCtx ctx = { CD_SYSTEM_DIR, DISK_SYSTEM_DIR, false };
    if (!iso9660ListDir(CD_SYSTEM_DIR, diffEntry, &ctx)) return true;
    return ctx.differs;
}

/* “从 CD 运行本轮”：不写盘，字体直接读自光驱并加载进 VBE */
void loadFontFromCdIntoVbe(void) {
    uint32_t size = 0;
    uint8_t* data = readCdFile(CD_FONT_PATH, &size);
    if (data) {
        if (size <= 256 * 17) vbeLoadFont(data, size);
        free(data);
    }

    /* 中文 16x16 字库随 system 目录分发，同样直接读自光驱 */
    uint32_t cjkSize = 0;
    uint8_t* cjk = readCdFile(CD_FONT_CJK_PATH, &cjkSize);
    if (cjk) {
        if (cjkSize >= 36) vbeLoadCjkFont(cjk, cjkSize);
        free(cjk);
    }
}