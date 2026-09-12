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
#include <disk.h>
#include <stdio/vga.h>
#include <stdio/vbe.h>
#include <serial.h>
#include <string/string.h>
#include <stdlib/stdlib.h>
#include <kernel.h>
#include <keyboard.h>
#include "device.h"

/* 安装目标：用户选定的磁盘与分区(由 selectDiskPartition 填充) */
static dDrive*    gInstDrive = NULL;
static Partition  gInstPart;
static bool       gInstPartValid = false;

/* 默认分区几何(min 分区) */
#define INST_DEFAULT_PART_GAP 2048   /* MBR 之后留给 GRUB 的 gap */
#define INST_MIN_PART         512    /* 最小可格式化分区扇区数 */

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

/* 十进制数转字符串(用于在向导行内拼装容量/起始值) */
static void u32ToStr(uint32_t value, char* out) {
    char tmp[12];
    int i = 0;
    if (value == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (value) { tmp[i++] = (char)('0' + value % 10); value /= 10; }
    int j = 0;
    while (i--) out[j++] = tmp[i];
    out[j] = '\0';
}

/* 前向声明(定义在 installSystem 之后) */
static bool installWriteGrub(void);
static bool installBootFiles(void);

/* 从光驱读取整个文件到动态内存，返回缓冲区与字节数。
 * 无 CD(U盘启动/光驱 AHCI)时回退到 GRUB 模块：CD 风格路径 "SYSTEM/FONT/FONT.BIN"
 * 转模块虚拟路径 "/system/font/font.bin"(匹配不区分大小写)。 */
static uint8_t* readCdFile(const char* path, uint32_t* outSize) {
    if (!iso9660Init()) {
        char modPath[96];
        modPath[0] = '/';
        int i = 0;
        for (; path[i] && i < 94; i++) modPath[i + 1] = path[i];
        modPath[i + 1] = '\0';
        uint32_t sz = 0;
        const uint8_t* m = fsFindModule(modPath, &sz);
        if (!m) { instLogLine("[CD] no CD and no module for file"); return 0; }
        uint8_t* buf = malloc(sz);
        if (!buf) { instLogLine("[CD] module out of memory"); return 0; }
        memcpy(buf, m, sz);
        *outSize = sz;
        instLog("[CD] from module: "); instLogLine(modPath);
        return buf;
    }

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
    if (!gInstPartValid || !gInstDrive) {
        instLogLine("[INSTALL] no target disk/partition selected");
        return false;
    }
    instLogLine("[INSTALL] Formatting partition (FAT32)...");
    if (!fat32Format(&fsVolume, gInstDrive, gInstPart.startLba, gInstPart.numSectors)) {
        instLogLine("[INSTALL] Format failed");
        return false;
    }

    /* 写完 FAT32 后再把 GRUB 引导代码覆盖到扇区 0(分区表一致，不破坏文件系统) */
    if (!installWriteGrub()) { return false; }

    if (!fat32Init(&fsVolume, gInstDrive)) { instLogLine("[INSTALL] Re-init failed"); return false; }

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

    /* boot.img 默认分区表为空，需补写选中的分区条目(否则盘不可引导) */
    mbrSetEntry(mbr, 0, &gInstPart);
    if (driveWriteSectors(gInstDrive, 0, 1, mbr) != 0) {
        instLogLine("[GRUB] write MBR failed");
        free(mbr);
        return false;
    }
    free(mbr);

    uint8_t* core = readCdFile(CD_GRUB_CORE_PATH, &sz);
    if (!core) { instLogLine("[GRUB] core.img not on CD"); return false; }
    uint32_t n = (sz + 511) / 512;

    /* 守卫：core.img 不得越过所选分区起始(保留分区完整性) */
    if (gInstPart.startLba <= GRUB_SECTOR_START + n) {
        instLogLine("[GRUB] partition too close to boot area");
        free(core);
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (driveWriteSectors(gInstDrive, GRUB_SECTOR_START + i, 1, core + (i * 512)) != 0) {
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

/* 安装进度：串口逐条记录；屏幕则固定在底部一行覆盖显示(含往复进度条动画)，
 * 避免拷贝大量文件时逐行换行刷屏。SCREEN_PROGRESS_ROW 为固定进度行。 */
#define SCREEN_PROGRESS_ROW (VGA_HEIGHT - 2)
static uint32_t sCopyDone = 0;

static void instWriteProgress(const char* tag, const char* name) {
    char buf[VGA_WIDTH + 1];
    memset(buf, ' ', VGA_WIDTH);
    buf[VGA_WIDTH] = '\0';
    int o = 0;
    buf[o++] = ' ';
    buf[o++] = '[';
    int barLen = 16;
    int fill = (int)(sCopyDone % (barLen + 1));
    for (int i = 0; i < barLen; i++) buf[o + i] = (i < fill) ? '#' : '-';
    o += barLen;
    buf[o++] = ']';
    buf[o++] = ' ';
    for (int i = 0; tag[i] && o < VGA_WIDTH - 1; i++) buf[o++] = tag[i];
    for (int i = 0; name[i] && o < VGA_WIDTH - 1; i++) buf[o++] = name[i];
    vgaSetCursorPos(SCREEN_PROGRESS_ROW, 0);
    vgaPutStr(buf);
}

static void instProgress(const char* tag, const char* name) {
    serialPutStr(tag);
    serialPutStr(name);
    serialPutStr("\n");
    sCopyDone++;
    instWriteProgress(tag, name);
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

/* 无 CD 时的 system 树固定清单(与 system/ 目录及 grub_cd.cfg 的 module2 行一致) */
static const char* const kModuleSysFiles[] = {
    "/system/font/font.bin",
    "/system/font/cjk16.bin",
    "/system/images/wallpaper.bmp",
    "/system/images/taskbar.bmp",
    "/system/images/vortex.bmp",
    "/system/images/mousepointer/arrow.bmp",
    "/system/images/mousepointer/textselect.bmp",
    "/system/programs/My_UI.elf",
};

/* 从 GRUB 模块拷贝 system 树到硬盘(U盘启动无 CD 时的安装源) */
static bool installCopySystemFromModules(void) {
    for (unsigned i = 0; i < sizeof(kModuleSysFiles)/sizeof(kModuleSysFiles[0]); i++) {
        const char* path = kModuleSysFiles[i];
        uint32_t sz = 0;
        const uint8_t* m = fsFindModule(path, &sz);
        if (!m) { instLogLine("[INSTALL] module missing"); instLogLine(path); return false; }
        /* 目录 = 路径去掉最后一段 */
        char dir[96]; strcpy(dir, path);
        char* sl = strrchr(dir, '/'); if (sl) *sl = '\0';
        if (!writeDiskFile(dir, path, m, sz)) return false;
        instProgress("[FILE] ", path);
    }
    instLogLine("[INSTALL] /system copied from modules");
    return true;
}

/* 把光驱 SYSTEM 目录整树拷贝到硬盘 /system(含字体)，
 * 拷贝前先清空硬盘 /system -> 完整替换 CD 系统文件夹。 */
static bool installCopySystem(void) {
    sCopyDone = 0;   /* 重置拷贝计数，驱动底部进度条 */
    if (!iso9660Init()) {
        /* 无 CD(U盘启动/光驱 AHCI)：从 GRUB 模块清单安装 */
        clearSystemDir();
        return installCopySystemFromModules();
    }

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

/* 把 font.bin 记录(17字节/条: 码 + 16 行)展开成 256×16 连续字形表并上传 VGA
 * 字模平面，使文本模式(Shell/菜单)也显示同一字体。0x00~0x1F 控制符无记录，
 * 保持空白字形。必须在进入 VBE 图形模式之前调用。 */
static void uploadFontRecordsToVga(const uint8_t* recs, uint32_t size) {
    uint8_t table[256 * 16];
    memset(table, 0, sizeof(table));
    for (uint32_t i = 0; i + 17 <= size && i + 17 <= 256 * 17; i += 17) {
        uint8_t code = recs[i];
        memcpy(&table[(size_t)code * 16], &recs[i + 1], 16);
    }
    vgaLoadFont(table);
}

/* 把硬盘上的 font.bin 上传到 VGA 字模平面 */
void loadFontIntoVga(void) {
    if (!fsVolume.valid) return;

    FileHandle f;
    if (!fsOpen(&f, DISK_FONT_PATH)) return;
    if (f.size <= 0 || f.size > 256 * 17) { fsClose(&f); return; }

    uint8_t* buf = malloc(f.size);
    if (!buf) { fsClose(&f); return; }
    int got = fsRead(&f, buf, f.size);
    fsClose(&f);

    if (got > 0) {
        uploadFontRecordsToVga(buf, (uint32_t)got);
        instLog("[FONT] uploaded to VGA planes: ");
        putDec32((uint32_t)got);
        instLogLine(" bytes");
    }
    free(buf);
}

/* “从 CD 运行本轮”：字体直接读自光驱上传到 VGA 字模平面 */
void loadFontFromCdIntoVga(void) {
    uint32_t size = 0;
    uint8_t* data = readCdFile(CD_FONT_PATH, &size);
    if (data) {
        if (size > 0 && size <= 256 * 17) {
            uploadFontRecordsToVga(data, size);
            instLog("[FONT] uploaded to VGA from CD\n");
        }
        free(data);
    }
}

/* ==================== 文本模式安装向导 ==================== */

static void instClear(void);
static bool cdSystemDiffers(void);

/* 设置底部固定选项行(先清空整行再写提示) */
static void instOptionBar(const char* hint) {
    static char spaces[VGA_WIDTH + 1];
    for (int i = 0; i < VGA_WIDTH; i++) spaces[i] = ' ';
    spaces[VGA_WIDTH] = '\0';
    vgaSetCursorPos(VGA_HEIGHT - 1, 0);
    vgaSetColorByte(theme);
    vgaPutStr(spaces);              /* 整行一次输出，避免逐字符全屏 diff */
    vgaSetCursorPos(VGA_HEIGHT - 1, 0);
    vgaPutStr(hint);
}

/* 在指定行水平居中输出字符串 */
static void instCenterRow(int row, const char* s) {
    int len = strlen(s);
    int col = (VGA_WIDTH - len) / 2;
    if (col < 0) col = 0;
    vgaSetCursorPos((uint8_t)row, (uint8_t)col);
    vgaPutStr(s);
}

/* 绘制顶部标题 + 全宽分隔线 */
static void instDrawHeader(const char* title) {
    static char eq[VGA_WIDTH + 1];
    for (int i = 0; i < VGA_WIDTH - 2; i++) eq[i] = '=';
    eq[VGA_WIDTH - 2] = '\0';
    vgaSetCursorPos(2, 0);
    vgaPutStr("  ");
    vgaPutStr(title);
    vgaSetCursorPos(3, 0);
    vgaPutStr(" ");                 /* 整行分隔线一次输出 */
    vgaPutStr(eq);
    vgaPutStr(" \n");
}

/* 在屏幕中央画一个带边框的选择框，文字行水平居中 */
static void instBox(int row, const char* const* lines, int nLines) {
    int maxlen = 0;
    for (int i = 0; i < nLines; i++) {
        int len = strlen(lines[i]);
        if (len > maxlen) maxlen = len;
    }
    int innerW = maxlen + 4;
    if (innerW > VGA_WIDTH - 4) innerW = VGA_WIDTH - 4;
    int left = (VGA_WIDTH - innerW) / 2;

    /* 顶边框(整行一次输出) */
    vgaSetCursorPos((uint8_t)row, (uint8_t)left);
    vgaPutStr("+");
    {
        static char dashes[VGA_WIDTH + 1];
        for (int i = 0; i < VGA_WIDTH; i++) dashes[i] = '-';
        dashes[VGA_WIDTH] = '\0';
        dashes[innerW - 2] = '\0';
        vgaPutStr(dashes);
    }
    vgaPutStr("+");

    /* 内容行 */
    for (int i = 0; i < nLines; i++) {
        int len = strlen(lines[i]);
        int pad = (innerW - 2 - len) / 2;
        char rbuf[VGA_WIDTH + 1];
        int o = 0;
        rbuf[o++] = '|';
        for (int p = 0; p < pad; p++) rbuf[o++] = ' ';
        for (int k = 0; k < len; k++) rbuf[o++] = lines[i][k];
        while (o < innerW - 1) rbuf[o++] = ' ';
        rbuf[o++] = '|';
        rbuf[o] = '\0';
        vgaSetCursorPos((uint8_t)(row + 1 + i), (uint8_t)left);
        vgaPutStr(rbuf);
    }

    /* 底边框(整行一次输出) */
    vgaSetCursorPos((uint8_t)(row + 1 + nLines), (uint8_t)left);
    vgaPutStr("+");
    {
        static char dashes2[VGA_WIDTH + 1];
        for (int i = 0; i < VGA_WIDTH; i++) dashes2[i] = '-';
        dashes2[VGA_WIDTH] = '\0';
        dashes2[innerW - 2] = '\0';
        vgaPutStr(dashes2);
    }
    vgaPutStr("+");
}

/* 绘制一页：顶部标题+分隔线、中央带边框问题、底部选项行 */
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

    instDrawHeader(title);
    instBox(7, lines, nLines);
    instOptionBar("Yes = Y/y    No = N/n    Back = Backspace");
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
            /* 诊断：未识别的按键(无意义或扫描码集合未匹配)打印十六进制，
             * 便于真机经串口判断键盘模式与按键码 */
            serialPutStr("[WIZARD] key=0x");
            serialPutHex8(c);
            serialPutStr("\n");
        }
        __asm__ volatile ("hlt");
    }
}

/* 数字选择(1..max 单选)；Backspace 返回 -1。
 * 提示条按当前选项数动态生成(不再是 Yes/No)，避免用户困惑按 y/n 无反应。 */
static int instAskIndex(const char* title, const char* const* lines, int nLines,
                        int min, int max) {
    instDrawPage(title, lines, nLines);
    static char bar[48];
    strcpy(bar, "Choose = ");
    {
        /* 生成 "1..N" 提示 */
        char lo[4]; u32ToStr((uint32_t)min, lo);
        char hi[4]; u32ToStr((uint32_t)max, hi);
        strcat(bar, lo);
        strcat(bar, "..");
        strcat(bar, hi);
    }
    strcat(bar, " (number key)    Back = Backspace");
    instOptionBar(bar);
    for (;;) {
        if (keyboardHasChar()) {
            unsigned char c = keyboardGetChar();
            if (c == '\b') return -1;
            if (c >= '0' && c <= '9') {
                int v = c - '0';
                if (v >= min && v <= max) return v;
            }
        }
        __asm__ volatile ("hlt");
    }
}

/* 选磁盘与分区：
 *   1) 无磁盘 -> 返回 false(向导走“从 CD 运行”)
 *   2) 选磁盘(编号)
 *   3) 扫描分区：有分区列出选择，无分区则创建默认分区(自 gap 2048 到盘尾)
 *   4) 确认格式化(Yes/No)
 * 结果写入 gInstDrive/gInstPart/gInstPartValid，返回 true 表示已选定目标。 */
static bool selectDiskPartition(void) {
    int nd = diskGetCount();
    if (nd <= 0) { instLogLine("[INSTALL] no writable disk found"); return false; }

    /* ---- 1. 选磁盘 ---- */
    int dsel;
    if (nd == 1) {
        dsel = 1;                       /* 只有一块盘：直接选中，跳过选择页 */
    } else {
        static char dNames[8][48];
        const char* dRows[8];
        for (int i = 0; i < nd; i++) {
            dDrive* d = diskGetDrive(i);
            char cap[16]; u32ToStr(d ? (d->capacityLba / 2 / 1024) : 0, cap);
            const char* t = (d && d->type == DRIVE_TYPE_AHCI) ? "SATA/AHCI"
                          : (d && d->type == DRIVE_TYPE_PIO_IDE) ? "IDE" : "Disk";
            strcpy(dNames[i], t);
            strcat(dNames[i], " drive, ");
            strcat(dNames[i], cap);
            strcat(dNames[i], " MB");
            dRows[i] = dNames[i];
        }
        dsel = instAskIndex("Select disk to format", dRows, nd, 1, nd);
    }
    if (dsel <= 0) return false;
    dDrive* drv = diskGetDrive(dsel - 1);
    if (!drv) { instLogLine("[INSTALL] bad disk"); return false; }
    gInstDrive = drv;

    /* ---- 2. 扫描分区 ---- */
    int np = diskScanPartitions(drv);

    /* ---- 3. 列出分区选择(含“新建整盘分区”选项) ---- */
    /* 选项数组：已有分区 + 一个“新建”项 */
    struct { uint32_t start, size, type; bool newPart; } opts[6];
    int nOpts = 0;
    for (int i = 0; i < np && nOpts < 6; i++) {
        opts[nOpts].start   = drv->parts[i].startLba;
        opts[nOpts].size    = drv->parts[i].numSectors;
        opts[nOpts].type    = drv->parts[i].type;
        opts[nOpts].newPart = false;
        nOpts++;
    }
    opts[nOpts].start   = INST_DEFAULT_PART_GAP;
    opts[nOpts].size    = (drv->capacityLba > INST_DEFAULT_PART_GAP)
                          ? (drv->capacityLba - INST_DEFAULT_PART_GAP) : 0;
    opts[nOpts].type    = 0x0C;
    opts[nOpts].newPart = true;
    nOpts++;

    int psel;
    if (nOpts == 1) {
        /* 盘上没有已有分区：唯一的选项就是“新建整盘分区”，直接选中 */
        psel = 1;
    } else {
        static char pDesc[6][48];
        const char* pRows[6];
        for (int i = 0; i < nOpts; i++) {
            char sz[16]; u32ToStr(opts[i].size / 2 / 1024, sz);
            char num[4]; u32ToStr((uint32_t)(i + 1), num);
            strcpy(pDesc[i], num);
            strcat(pDesc[i], ": ");
            if (opts[i].newPart) {
                strcat(pDesc[i], "* create new FAT32 partition");
            } else {
                strcat(pDesc[i], "part type=");
                {
                    char tb[4];
                    tb[0] = '0' + (opts[i].type >> 4 < 10 ? opts[i].type >> 4 : 0);
                    /* 简化显示：直接用十六进制高位 */
                    tb[1] = '\0';
                    strcat(pDesc[i], tb);
                }
                strcat(pDesc[i], " start=");
                { char sb[16]; u32ToStr(opts[i].start, sb); strcat(pDesc[i], sb); }
                strcat(pDesc[i], " size=");
                strcat(pDesc[i], sz);
                strcat(pDesc[i], "MB");
            }
            pRows[i] = pDesc[i];
        }
        psel = instAskIndex("Select partition", pRows, nOpts, 1, nOpts);
    }
    if (psel <= 0) return false;
    gInstPart.startLba   = opts[psel - 1].start;
    gInstPart.numSectors = opts[psel - 1].size;
    gInstPart.type       = (uint8_t)opts[psel - 1].type;
    gInstPart.bootable   = 0x80;
    gInstPart.slot       = 0;

    if (gInstPart.numSectors < INST_MIN_PART) {
        instLogLine("[INSTALL] target partition too small");
        return false;
    }

    /* ---- 4. 确认格式化 ---- */
    static const char* const confLines[] = {
        "This will ERASE the selected partition",
        "and install VortexOS onto it.",
        "",
        "Continue?",
    };
    gInstPartValid = false;
    for (;;) {
        InstChoice cc = instAsk("Confirm format", confLines, 4);
        if (cc == INST_CHOICE_YES) {
            gInstPartValid = true;
            instLog("[INSTALL] target disk=");
            putDec32((uint32_t)(psel));          /* 无用回显，占位 */
            instLog(" partition start=");
            putDec32(gInstPart.startLba);
            instLog(" size=");
            putDec32(gInstPart.numSectors);
            instLogLine(" sect");
            return true;
        }
        if (cc == INST_CHOICE_NO) return false;
        /* Back: 重新选分区 */
        return false;
    }
}

/* 安装完成提示并短暂停顿后重启 */
static void instRebootSoon(void) {
    instClear();
    instCenterRow(10, "========================================");
    instCenterRow(11, "  Installation complete!  ");
    instCenterRow(12, "Rebooting in a few seconds...");
    instCenterRow(13, "========================================");
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
    instCenterRow(3, "Formatting and installing VortexOS...");
    instCenterRow(4, "Please wait, this may take a moment.");
    vgaSetCursorPos(7, 0);
    return installSystem();
}

/* 更新：清空 /system 后整树重拷(不重新分区/格式化) */
static bool installUpdate(void) {
    instClear();
    instCenterRow(3, "Updating system from CD-ROM...");
    instCenterRow(4, "Please wait.");
    vgaSetCursorPos(7, 0);
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
            /* 先选磁盘与分区，再格式化安装(支持多磁盘/多分区) */
            if (selectDiskPartition()) {
                if (installFresh()) { instRebootSoon(); }
            }
            continue;              /* 未选或安装失败则重新询问 */
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