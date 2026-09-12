// vbe.c
// VBE(dispi) 显卡驱动：通过 QEMU/Bochs 显卡的 VBE 硬件扩展端口探测并进入图形模式。
// 使用线性帧缓冲(LFB, 即 PCI BAR0)直接映射，32bpp，无需切换到实模式调用 BIOS。
#include "vbe.h"
#include <usb/pci.h>
#include <io.h>
#include <stdio/vga.h>
#include <serial.h>
#include <keyboard.h>
#include <mm/paging.h>
#include <math/math.h>
#include <string/string.h>
#include <stdlib/stdlib.h>

/* 解锁/使能控制字（值域，不对外暴露） */
#define VBE_ID_LCK        0xB0C1   /* 写入该 ID 可解锁 VBE 寄存器 */
#define VBE_ID_MASK       0xFFF0   /* 版本号高 12 位匹配依据 */
#define VBE_ID_BASE       0xB0C0   /* 最小的合法版本号 */
#define VBE_CMD_ENABLE    0x01
#define VBE_CMD_LFB       0x40

VbeInfo gVbeInfo;
static bool sReady = false;
/* 帧缓冲是否由引导器(GRUB Multiboot2 tag)提供：是则不再(也无法)经 dispi 端口改模式 */
static bool sFromBootloader = false;

/* 8x16 字形表：由 vbeLoadFont() 从 font.bin(每字符17字节: 码+16行)注入，
 * 供 vbeDrawString() 使用。未注入时回退到内置 8x8 点阵。 */
static uint8_t sFontGlyph[256][16];
static bool    sFont8x16 = false;

/* 16x16 CJK 点阵字库：由 vbeLoadCjkFont() 从 cjk16.bin 注入，供 vbeDrawStringCJK() 使用。
 * 内部保存当前字形缓冲指针与记录数；字形按码点升序存储，绘制时用二分查找。 */
static uint8_t*  sCjkGlyphs   = NULL;   /* 每条 36 字节: [码点_LE4][32 字节点阵] */
static uint32_t   sCjkCount    = 0;     /* 记录(字符)数 */
static uint32_t   sCjkCapacity = 0;     /* 已分配字节数 */

/* 当前绘制色：图形模式没有前景/背景色之分，先 vbeSetColor 设置再绘制，
 * 所有绘图/文本函数均使用该颜色。32bpp 为 0x00RRGGBB。 */
static uint32_t sColor = 0x00FFFFFF;  /* 默认白色 */

/* 渲染目标重定向：非空时所有绘图函数写入该 RAM 缓冲(同 LFB 的 32bpp 布局)，
 * 为空时写入真实线性帧缓冲。用于“建一块不受 LFB 可读性影响的全量快照”。 */
static uint32_t* sTarget = NULL;

/* 窗口客户区 surface 渲染目标：非空时优先级最高，所有绘图函数写入客户区
 * RAM 像素缓冲。屏幕坐标 (sWinX,sWinY)..(sWinX+sWinW-1, sWinY+sWinH-1)
 * 对应 buffer 的 [0..sWinH-1][0..sWinW-1]，行宽 sWinStride(像素)。
 * 绘制坐标仍用屏幕坐标，内部统一换算并裁剪到客户区。 */
static uint32_t* sWinBuffer = NULL;
static int       sWinStride = 0;
static int       sWinX = 0, sWinY = 0, sWinW = 0, sWinH = 0;

void vbeBeginRamFrame(uint32_t* buffer) {
    sTarget = buffer;
}

void vbeEndRamFrame(void) {
    sTarget = NULL;
}

void vbeBeginRamWindow(uint32_t* buffer, int stride, int x, int y, int w, int h) {
    sWinBuffer = buffer;
    sWinStride = stride;
    sWinX = x; sWinY = y; sWinW = w; sWinH = h;
    sTarget = NULL;   /* 窗口目标优先于整帧 RAM 目标 */
}

void vbeEndRamWindow(void) {
    sWinBuffer = NULL;
}

/* 是否有可写的渲染目标(窗口 surface / 已使能的 32bpp 图形模式) */
static bool vbeRenderable(void) {
    return sWinBuffer || (gVbeInfo.enabled && gVbeInfo.bpp == 32);
}

/* 返回屏幕坐标 (x,y) 像素在当前渲染目标中的写入地址；越界返回 NULL。
 * 内部自动分派到 窗口surface / 整帧RAM / 真实LFB 三种目标。 */
static uint32_t* vbePixelAddr(int x, int y) {
    if (sWinBuffer) {
        int lx = x - sWinX;
        int ly = y - sWinY;
        if (lx < 0 || lx >= sWinW || ly < 0 || ly >= sWinH) return NULL;
        return &sWinBuffer[(size_t)ly * sWinStride + lx];
    }
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32) return NULL;
    if (x < 0 || y < 0 || (uint32_t)x >= gVbeInfo.xres || (uint32_t)y >= gVbeInfo.yres)
        return NULL;
    uint32_t* fb = sTarget ? sTarget : (uint32_t*)gVbeInfo.lfbAddr;
    uint32_t rowPix = gVbeInfo.pitch ? (gVbeInfo.pitch / 4u) : gVbeInfo.xres;
    return &fb[(size_t)y * rowPix + x];
}

/* 当前渲染目标在屏幕坐标系中的可写矩形 [x0,y0] .. [x1,y1) */
static void vbeTargetClip(int* x0, int* y0, int* x1, int* y1) {
    if (sWinBuffer) {
        *x0 = sWinX;  *y0 = sWinY;
        *x1 = sWinX + sWinW;  *y1 = sWinY + sWinH;
    } else {
        *x0 = 0;  *y0 = 0;
        *x1 = gVbeInfo.xres;  *y1 = gVbeInfo.yres;
    }
}

/* ===== 颜色状态 ===== */
void vbeSetColor(uint32_t color) { sColor = color; }
uint32_t vbeGetColor(void) { return sColor; }
void vbeSetColorRgb(uint8_t r, uint8_t g, uint8_t b) { sColor = vbeColor(r, g, b); }

int vbeWidth  = 1024;
int vbeHeight = 768;

/* 8x8 点阵字体（ASCII 0x20~0x7E），每字符 8 字节，每字节为一行，
 * 位 7 为最左像素，位 0 为最右像素。 */
static const uint8_t sFont8x8[95][8] = {
    /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 '!' */ {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    /* 0x22 '"' */ {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    /* 0x23 '#' */ {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    /* 0x24 '$' */ {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    /* 0x25 '%' */ {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
    /* 0x26 '&' */ {0x30,0x48,0x48,0x30,0x4A,0x44,0x3A,0x00},
    /* 0x27 '\''*/ {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 0x28 '(' */ {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    /* 0x29 ')' */ {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    /* 0x2A '*' */ {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00},
    /* 0x2B '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    /* 0x2C ',' */ {0x00,0x00,0x00,0x00,0x30,0x30,0x18,0x00},
    /* 0x2D '-' */ {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    /* 0x2E '.' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    /* 0x2F '/' */ {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    /* 0x30 '0' */ {0x3C,0x42,0x46,0x5A,0x62,0x42,0x3C,0x00},
    /* 0x31 '1' */ {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    /* 0x32 '2' */ {0x3C,0x42,0x02,0x0C,0x30,0x40,0x7E,0x00},
    /* 0x33 '3' */ {0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0x00},
    /* 0x34 '4' */ {0x0C,0x14,0x24,0x44,0x7E,0x04,0x04,0x00},
    /* 0x35 '5' */ {0x7E,0x40,0x78,0x04,0x02,0x44,0x38,0x00},
    /* 0x36 '6' */ {0x1C,0x20,0x40,0x7C,0x42,0x42,0x3C,0x00},
    /* 0x37 '7' */ {0x7E,0x42,0x04,0x08,0x10,0x10,0x10,0x00},
    /* 0x38 '8' */ {0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0x00},
    /* 0x39 '9' */ {0x3C,0x42,0x42,0x3E,0x02,0x04,0x38,0x00},
    /* 0x3A ':' */ {0x00,0x18,0x00,0x00,0x00,0x18,0x00,0x00},
    /* 0x3B ';' */ {0x00,0x18,0x00,0x00,0x18,0x18,0x10,0x00},
    /* 0x3C '<' */ {0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00},
    /* 0x3D '=' */ {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    /* 0x3E '>' */ {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00},
    /* 0x3F '?' */ {0x3C,0x42,0x02,0x0C,0x18,0x00,0x18,0x00},
    /* 0x40 '@' */ {0x3C,0x42,0x5A,0x5E,0x5A,0x40,0x3C,0x00},
    /* 0x41 'A' */ {0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x00},
    /* 0x42 'B' */ {0x78,0x44,0x44,0x7C,0x42,0x42,0x7C,0x00},
    /* 0x43 'C' */ {0x1C,0x22,0x40,0x40,0x40,0x22,0x1C,0x00},
    /* 0x44 'D' */ {0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00},
    /* 0x45 'E' */ {0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00},
    /* 0x46 'F' */ {0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x00},
    /* 0x47 'G' */ {0x1C,0x22,0x40,0x4E,0x42,0x22,0x1E,0x00},
    /* 0x48 'H' */ {0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00},
    /* 0x49 'I' */ {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* 0x4A 'J' */ {0x02,0x02,0x02,0x02,0x42,0x42,0x3C,0x00},
    /* 0x4B 'K' */ {0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00},
    /* 0x4C 'L' */ {0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00},
    /* 0x4D 'M' */ {0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0x00},
    /* 0x4E 'N' */ {0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00},
    /* 0x4F 'O' */ {0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00},
    /* 0x50 'P' */ {0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00},
    /* 0x51 'Q' */ {0x3C,0x42,0x42,0x52,0x4A,0x44,0x3A,0x00},
    /* 0x52 'R' */ {0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00},
    /* 0x53 'S' */ {0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00},
    /* 0x54 'T' */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* 0x55 'U' */ {0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00},
    /* 0x56 'V' */ {0x42,0x42,0x42,0x42,0x42,0x24,0x18,0x00},
    /* 0x57 'W' */ {0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00},
    /* 0x58 'X' */ {0x42,0x42,0x24,0x18,0x24,0x42,0x42,0x00},
    /* 0x59 'Y' */ {0x42,0x42,0x24,0x18,0x18,0x18,0x18,0x00},
    /* 0x5A 'Z' */ {0x7E,0x02,0x04,0x18,0x20,0x40,0x7E,0x00},
    /* 0x5B '[' */ {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    /* 0x5C '\' */ {0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
    /* 0x5D ']' */ {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    /* 0x5E '^' */ {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
    /* 0x5F '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00},
    /* 0x60 '`' */ {0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x61 'a' */ {0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00},
    /* 0x62 'b' */ {0x40,0x40,0x7C,0x42,0x42,0x42,0x7C,0x00},
    /* 0x63 'c' */ {0x00,0x00,0x3C,0x40,0x40,0x40,0x3C,0x00},
    /* 0x64 'd' */ {0x02,0x02,0x3E,0x42,0x42,0x42,0x3E,0x00},
    /* 0x65 'e' */ {0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00},
    /* 0x66 'f' */ {0x0C,0x12,0x10,0x38,0x10,0x10,0x10,0x00},
    /* 0x67 'g' */ {0x00,0x3E,0x42,0x42,0x42,0x3E,0x02,0x3C},
    /* 0x68 'h' */ {0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x00},
    /* 0x69 'i' */ {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    /* 0x6A 'j' */ {0x06,0x00,0x06,0x06,0x06,0x46,0x3C,0x00},
    /* 0x6B 'k' */ {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00},
    /* 0x6C 'l' */ {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /* 0x6D 'm' */ {0x00,0x00,0x6C,0x5A,0x5A,0x5A,0x5A,0x00},
    /* 0x6E 'n' */ {0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x00},
    /* 0x6F 'o' */ {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00},
    /* 0x70 'p' */ {0x00,0x7C,0x42,0x42,0x42,0x7C,0x40,0x40},
    /* 0x71 'q' */ {0x00,0x3E,0x42,0x42,0x42,0x3E,0x02,0x02},
    /* 0x72 'r' */ {0x00,0x00,0x5C,0x62,0x40,0x40,0x40,0x00},
    /* 0x73 's' */ {0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00},
    /* 0x74 't' */ {0x10,0x10,0x7C,0x10,0x10,0x12,0x0C,0x00},
    /* 0x75 'u' */ {0x00,0x00,0x42,0x42,0x42,0x42,0x3E,0x00},
    /* 0x76 'v' */ {0x00,0x00,0x42,0x42,0x42,0x24,0x18,0x00},
    /* 0x77 'w' */ {0x00,0x00,0x42,0x42,0x5A,0x5A,0x24,0x00},
    /* 0x78 'x' */ {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00},
    /* 0x79 'y' */ {0x00,0x42,0x42,0x42,0x42,0x3E,0x02,0x3C},
    /* 0x7A 'z' */ {0x00,0x00,0x7E,0x04,0x18,0x20,0x7E,0x00},
    /* 0x7B '{' */ {0x0C,0x18,0x18,0x30,0x18,0x18,0x0C,0x00},
    /* 0x7C '|' */ {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* 0x7D '}' */ {0x30,0x18,0x18,0x0C,0x18,0x18,0x30,0x00},
    /* 0x7E '~' */ {0x3A,0x6C,0x00,0x00,0x00,0x00,0x00,0x00},
};

static void vbeWrite(uint16_t index, uint16_t value) {
    outw(VBE_INDEX_PORT, index);
    outw(VBE_DATA_PORT, value);
}

static uint16_t vbeRead(uint16_t index) {
    outw(VBE_INDEX_PORT, index);
    return inw(VBE_DATA_PORT);
}

/* 把帧缓冲物理地址身份映射进页表，供内核直接读写 */
static void vbeMapLfb(void) {
    if (!gVbeInfo.lfbAddr || !gVbeInfo.vramSize) return;
    for (uint32_t a = gVbeInfo.lfbAddr; a < gVbeInfo.lfbAddr + gVbeInfo.vramSize; a += 0x1000) {
        pagingMapPage(a, a, PAGE_PRESENT | PAGE_WRITABLE);
    }
}

/* ===== 帧缓冲文本低级输出(bpp 无关) =====
 * 与 32bpp 图形管线解耦：无论 GRUB 提供 8/16/24/32bpp，都能把 80x25 控制台
 * 文字画上屏(不经过 vbePixelAddr 的 bpp==32 门禁)。真机显卡常在 24/16bpp 下
 * 提供帧缓冲，此前因强制 32bpp 拒用其帧缓冲导致黑屏。
 * 只读 gVbeInfo(lfbAddr/pitch/xres/yres/bpp) 与字形表。 */

static void vbeFbStorePixel(int x, int y, uint32_t rgb) {
    if (!gVbeInfo.lfbAddr || gVbeInfo.bpp == 0) return;
    if (x < 0 || y < 0 || (uint32_t)x >= gVbeInfo.xres || (uint32_t)y >= gVbeInfo.yres) return;
    uint32_t pitch = gVbeInfo.pitch ? gVbeInfo.pitch : gVbeInfo.xres * 4u;
    uint8_t* row = (uint8_t*)gVbeInfo.lfbAddr + (size_t)y * pitch + (size_t)x * (gVbeInfo.bpp / 8u);
    switch (gVbeInfo.bpp) {
        case 32:
            *(uint32_t*)row = rgb;
            break;
        case 24:
            row[0] = (uint8_t)(rgb);         /* B */
            row[1] = (uint8_t)(rgb >> 8);    /* G */
            row[2] = (uint8_t)(rgb >> 16);   /* R */
            break;
        case 16: {   /* RGB565 */
            uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
            *(uint16_t*)row = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            break;
        }
        case 15: {   /* RGB555 */
            uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
            *(uint16_t*)row = (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
            break;
        }
        case 8:      /* 8bpp 灰度近似，不一定精确但可辨识文本 */
            *row = (uint8_t)((((rgb >> 16) & 0xFF) * 77 + ((rgb >> 8) & 0xFF) * 150 +
                              (rgb & 0xFF) * 29) >> 8);
            break;
        default:
            break;
    }
}

/* 直接向帧缓冲填充实心矩形，忽略 32bpp 门禁(bpp 无关) */
void vbeFbFillRect(int x, int y, int w, int h, uint32_t rgb) {
    /* 32bpp 快速路径：整行 4 字节连续写入，避免逐像素函数调用+边界检查开销
     * (vgaOutputFlush 每格背景+字形要写 256 像素，慢路径是卡顿主因) */
    if (gVbeInfo.lfbAddr && gVbeInfo.bpp == 32) {
        uint32_t pitch = gVbeInfo.pitch ? gVbeInfo.pitch : gVbeInfo.xres * 4u;
        int x2 = x + w, y2 = y + h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x2 > (int)gVbeInfo.xres) x2 = (int)gVbeInfo.xres;
        if (y2 > (int)gVbeInfo.yres) y2 = (int)gVbeInfo.yres;
        if (x2 <= x || y2 <= y) return;
        uint32_t stride = pitch / 4u;
        uint32_t* base = (uint32_t*)gVbeInfo.lfbAddr + (size_t)y * stride + (uint32_t)x;
        for (int yy = y; yy < y2; yy++) {
            uint32_t* p = base + (size_t)(yy - y) * stride;
            for (int xx = x; xx < x2; xx++) p[xx - x] = rgb;
        }
        return;
    }
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            vbeFbStorePixel(xx, yy, rgb);
}

/* 向帧缓冲绘制一个字符格(8xW 字形，前景 fg / 背景 bg)，bpp 无关 */
void vbeFbTextCell(int x, int y, uint8_t ch, uint32_t fg, uint32_t bg) {
    int H = sFont8x16 ? 16 : 8;
    uint8_t rows[16];
    if (sFont8x16) {
        for (int i = 0; i < 16; i++) rows[i] = sFontGlyph[ch][i];
    } else {
        int idx = (int)(ch - 0x20);
        if (idx < 0 || idx >= 95) idx = 0;
        for (int i = 0; i < 8; i++) rows[i] = sFont8x8[idx][i];
        for (int i = 8; i < 16; i++) rows[i] = 0;
    }
    /* 32bpp 快速路径 */
    if (gVbeInfo.lfbAddr && gVbeInfo.bpp == 32 &&
        x >= 0 && y >= 0 && x + VBE_FONT_W <= (int)gVbeInfo.xres &&
        y + H <= (int)gVbeInfo.yres) {
        uint32_t pitch = gVbeInfo.pitch ? gVbeInfo.pitch : gVbeInfo.xres * 4u;
        uint32_t stride = pitch / 4u;
        uint32_t* base = (uint32_t*)gVbeInfo.lfbAddr + (size_t)y * stride + (uint32_t)x;
        for (int row = 0; row < H; row++) {
            uint8_t bits = rows[row];
            uint32_t* p = base + (size_t)row * stride;
            for (int col = 0; col < VBE_FONT_W; col++)
                p[col] = (bits & (0x80 >> col)) ? fg : bg;
        }
        return;
    }
    for (int row = 0; row < H; row++) {
        uint8_t bits = rows[row];
        for (int col = 0; col < VBE_FONT_W; col++) {
            int px = x + col, py = y + row;
            vbeFbStorePixel(px, py, (bits & (0x80 >> col)) ? fg : bg);
        }
    }
}

int vbeInit(void) {
    if (sReady) return 0;

    /* 经 PCI 找到显卡(BAR0 即线性帧缓冲) */
    pciDevice vga;
    if (pciFindClass(&vga, PCI_CLASS_DISPLAY) != 0 || !vga.bar0Addr) {
        serialPutStr("[VBE] no framebuffer controller found\n");
        return -1;
    }

    gVbeInfo.lfbAddr = (uint32_t)vga.bar0Addr;
    gVbeInfo.vramSize = vga.bar0Size;
    gVbeInfo.enabled = 0;

    serialPutStr("[VBE] LFB=");
    serialPutHex32(gVbeInfo.lfbAddr);
    serialPutStr(" VRAM=");
    serialPutHex32(gVbeInfo.vramSize);
    serialPutStr(" bytes\n");

    /* 写入解锁 ID 并读回版本，确认接口可用 */
    vbeWrite(VBE_DISPI_ID, VBE_ID_LCK);
    uint16_t id = vbeRead(VBE_DISPI_ID);
    if ((id & VBE_ID_MASK) != (VBE_ID_BASE & VBE_ID_MASK)) {
        serialPutStr("[VBE] not responding\n");
        return -1;
    }

    sReady = true;
    return 0;
}

bool vbeReady(void) {
    return sReady;
}

/* 使用引导器(GRUB)提供的帧缓冲：真机显卡无 Bochs dispi 端口(BAR0 也不是 LFB)，
 * 无法经 vbeInit/vbeSetMode 进入图形。此时直接采用 GRUB 已就绪的线性帧缓冲。 */
int vbeUseBootloaderFramebuffer(uint32_t fbAddr, uint32_t pitch,
                                uint32_t xres, uint32_t yres, uint32_t bpp) {
    if (!fbAddr || !xres || !yres) return -1;

    gVbeInfo.lfbAddr  = fbAddr;
    gVbeInfo.vramSize = (pitch ? pitch : (uint32_t)xres * 4u) * yres;
    gVbeInfo.pitch    = pitch;
    gVbeInfo.xres     = (uint16_t)xres;
    gVbeInfo.yres     = (uint16_t)yres;
    gVbeInfo.bpp      = (uint16_t)bpp;
    gVbeInfo.enabled  = 1;
    sReady            = true;
    sFromBootloader   = true;
    /* 立即用真实分辨率同步全局尺寸：帧缓冲文本(vgaOutputFlush)按这两个值把
     * 80x25 控制台居中。若不同步，早前的启动/菜单文本会用默认 1024x768 的居中
     * 偏移落笔，随后 vbeSetMode 改成真实尺寸导致整体偏移、旧像素残留成鬼影。 */
    vbeWidth  = (int)xres;
    vbeHeight = (int)yres;
    vbeMapLfb();
    return 0;
}

void vbeSyncInfo(void) {
    if (!sReady) vbeInit();
    if (sFromBootloader) return;   /* 无 dispi 端口可读，信息已在 setup 时填好 */
    /* 显存以 64K 为单位，任何状态都可读 */
    gVbeInfo.vramSize = (uint32_t)vbeRead(VBE_DISPI_MEM64K) * 65536u;
    /* 分辨率/色深仅在图形(LFB)模式下有意义 */
    if (gVbeInfo.enabled) {
        gVbeInfo.xres = vbeRead(VBE_DISPI_XRES);
        gVbeInfo.yres = vbeRead(VBE_DISPI_YRES);
        gVbeInfo.bpp  = vbeRead(VBE_DISPI_BPP);
        gVbeInfo.pitch = (uint32_t)gVbeInfo.xres * 4u;
    }
}

/* VGA 标准寄存器端口 */
#define VGA_MISC_WRITE 0x03C2
#define VGA_SEQ_INDEX  0x03C4
#define VGA_SEQ_DATA   0x03C5
#define VGA_GFX_INDEX  0x03CE
#define VGA_GFX_DATA   0x03CF
#define VGA_CRTC_INDEX 0x03D4
#define VGA_CRTC_DATA  0x03D5
#define VGA_ATTRIB_IO  0x03C0
#define VGA_INPUT_READ 0x03DA

/* 把标准 VGA 重新编程为 80x25 彩色文本模式(Mode 3)。
 * 部分 QEMU/固件在 VBE disable 后不回文本模式，需显式恢复全部分频/定序/CRTC/属性寄存器。 */
static void vbeForceTextMode(void) {
    /* 1. Misc 输出寄存器：80x25 彩色文本(9 点字宽，28.322MHz) */
    outb(VGA_MISC_WRITE, 0x67);

    /* 2. 定序器(Sequencer) */
    outb(VGA_SEQ_INDEX, 0x00); outb(VGA_SEQ_DATA, 0x03); /* Reset */
    outb(VGA_SEQ_INDEX, 0x01); outb(VGA_SEQ_DATA, 0x00); /* Clocking */
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x03); /* Map Mask: 平面 0,1 */
    outb(VGA_SEQ_INDEX, 0x03); outb(VGA_SEQ_DATA, 0x00); /* Char Map */
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x03); /* Memory Mode */

    /* 3. 图形控制器(Graphics Controller)：文本模式寻址/读写 */
    outb(VGA_GFX_INDEX, 0x00); outb(VGA_GFX_DATA, 0x00); /* Set/Reset */
    outb(VGA_GFX_INDEX, 0x01); outb(VGA_GFX_DATA, 0x00); /* Enable Set/Reset */
    outb(VGA_GFX_INDEX, 0x02); outb(VGA_GFX_DATA, 0x00); /* Color Compare */
    outb(VGA_GFX_INDEX, 0x03); outb(VGA_GFX_DATA, 0x00); /* Data Rotate */
    outb(VGA_GFX_INDEX, 0x04); outb(VGA_GFX_DATA, 0x00); /* Read Map Select */
    outb(VGA_GFX_INDEX, 0x05); outb(VGA_GFX_DATA, 0x10); /* Mode: 文本模式 */
    outb(VGA_GFX_INDEX, 0x06); outb(VGA_GFX_DATA, 0x0E); /* Misc: 偶奇平面/链式 */
    outb(VGA_GFX_INDEX, 0x07); outb(VGA_GFX_DATA, 0x00); /* Color Don't Care */
    outb(VGA_GFX_INDEX, 0x08); outb(VGA_GFX_DATA, 0xFF); /* Bit Mask */

    /* 4. CRTC：先解除写保护(清 reg 0x11 的 bit7)，再写 80x25 时序 */
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & 0x7F);
    static const uint8_t crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,  /* 0x00-0x07 */
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,  /* 0x08-0x0F */
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,  /* 0x10-0x17 */
        0xFF                                            /* 0x18 */
    };
    for (uint8_t i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, crtc[i]);
    }

    /* 5. 属性控制器(Attribute Controller)：读 3DA 复位地址翻转，再写调色板与模式 */
    (void)inb(VGA_INPUT_READ);
    for (uint8_t i = 0; i < 16; i++) {   /* 调色板：恒等映射 */
        outb(VGA_ATTRIB_IO, i);
        outb(VGA_ATTRIB_IO, i);
    }
    outb(VGA_ATTRIB_IO, 0x10); outb(VGA_ATTRIB_IO, 0x0C); /* 文本模式控制 */
    outb(VGA_ATTRIB_IO, 0x11); outb(VGA_ATTRIB_IO, 0x00); /* 溢出扫描色 */
    outb(VGA_ATTRIB_IO, 0x12); outb(VGA_ATTRIB_IO, 0x0F); /* 颜色平面使能 */
    outb(VGA_ATTRIB_IO, 0x13); outb(VGA_ATTRIB_IO, 0x08); /* 水平像素平移 */
    outb(VGA_ATTRIB_IO, 0x14); outb(VGA_ATTRIB_IO, 0x00); /* 颜色选择 */
    outb(VGA_ATTRIB_IO, 0x20);                             /* 结束：关闭调色板访问 */
}

int vbeSetMode(uint16_t xres, uint16_t yres, uint16_t bpp) {
    /* 帧缓冲已由引导器提供并映射：分辨率由 GRUB 决定，无法(也无需)经
     * dispi 端口重设，直接采用之。图形管线仅支持 32bpp，其他色深返回失败，
     * 由调用方提示无法进入图形(控制台文本仍可经 vbeFbTextCell 正常显示)。 */
    if (sFromBootloader) {
        vbeWidth  = gVbeInfo.xres;
        vbeHeight = gVbeInfo.yres;
        if (gVbeInfo.bpp != 32) return -1;
        gVbeInfo.enabled = 1;
        return 0;
    }

    if (!vbeReady() && vbeInit() != 0) return -1;

    /* 顺序：宽度 → 高度 → 色深 → 使能(带 LFB) */
    vbeWrite(VBE_DISPI_XRES, xres);
    vbeWrite(VBE_DISPI_YRES, yres);
    vbeWrite(VBE_DISPI_BPP,  bpp);
    vbeWrite(VBE_DISPI_ENABLE, VBE_CMD_ENABLE | VBE_CMD_LFB);

    gVbeInfo.xres = vbeRead(VBE_DISPI_XRES);
    gVbeInfo.yres = vbeRead(VBE_DISPI_YRES);
    gVbeInfo.bpp  = vbeRead(VBE_DISPI_BPP);
    gVbeInfo.pitch = (uint32_t)gVbeInfo.xres * 4u;
    gVbeInfo.enabled = 1;

    /* vramSize 可能被 vbeSyncInfo() 用不可靠的 dispi MEM64K 值覆盖成过小值
     * (QEMU 下会从 16MB 缩成 288KB)，导致 vbeMapLfb 只映射前小段帧缓冲，
     * 超出部分写入即页错误。这里保证至少映射当前模式所需大小。 */
    uint32_t need = (uint32_t)xres * yres * (bpp / 8u);
    if (gVbeInfo.vramSize < need) gVbeInfo.vramSize = need;

    vbeMapLfb();
    return 0;
}

void vbeDisable(void) {
    if (sFromBootloader) {
        /* 无 dispi 端口：仅标记图形关闭；真机退出图形返回文本菜单由 GRUB 的
         * 文本模式承担，这里不写端口避免干扰实机显卡状态。 */
        gVbeInfo.enabled = 0;
        return;
    }
    vbeWrite(VBE_DISPI_ENABLE, 0);   /* 回到文本模式 */
    vbeForceTextMode();              /* 显式恢复 80x25 文本模式，弥补个别固件不回文本的情况 */
    gVbeInfo.enabled = 0;
}

/* 用当前绘制色清屏(即填满整个当前渲染目标) */
void vbeClearScreen(void) {
    if (!vbeRenderable()) return;
    int x0, y0, x1, y1;
    vbeTargetClip(&x0, &y0, &x1, &y1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = vbePixelAddr(x0, yy);
        if (!row) continue;
        for (int xx = x0; xx < x1; xx++) row[xx - x0] = sColor;
    }
}

/* 用当前绘制色绘制单个像素 */
void vbeDrawPixel(uint32_t x, uint32_t y) {
    uint32_t* p = vbePixelAddr((int)x, (int)y);
    if (p) *p = sColor;
}

/* 用当前绘制色填充矩形，自动裁剪到当前渲染目标 */
void vbeDrawFillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!vbeRenderable()) return;

    /* 与当前渲染目标矩形求交，避免越界写 */
    int x0, y0, x1, y1;
    vbeTargetClip(&x0, &y0, &x1, &y1);
    if ((int)x      > x0) x0 = (int)x;
    if ((int)y      > y0) y0 = (int)y;
    if ((int)x + (int)w < x1) x1 = (int)x + (int)w;
    if ((int)y + (int)h < y1) y1 = (int)y + (int)h;
    if (x0 >= x1 || y0 >= y1) return;

    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = vbePixelAddr(x0, yy);
        if (!row) continue;
        for (int xx = x0; xx < x1; xx++) row[xx - x0] = sColor;
    }
}

/* 线框矩形：四边画在 (x,y) 起始、宽 w、高 h 的矩形内部，
 * 右下角止于 x+w-1 / y+h-1，恰好与 vbeDrawFillRect 的覆盖一致，
 * 不越过边界外那 1 像素(否则窗口右/下边框会冒出 w×h 之外，
 * 局部刷新时漏刷其残留 → 残影)。 */
void vbeDrawLineRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w < 1 || h < 1) return;
    uint32_t x1 = x + w - 1;
    uint32_t y1 = y + h - 1;
    vbeDrawLine(x, y, x1, y);
    vbeDrawLine(x, y, x, y1);
    vbeDrawLine(x1, y, x1, y1);
    vbeDrawLine(x, y1, x1, y1);
}

/* 把自顶向下、BGRA(蓝|绿|红|alpha) 的像素缓冲按 Alpha 混合绘制到当前渲染目标。
 * 越界区域自动裁剪；alpha 为 0 跳过、255 直接覆盖、其余按比例混合。
 * bgra 一行紧接着一行，每像素 4 字节。 */
void vbeDrawBitmap(uint32_t x, uint32_t y, const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!vbeRenderable() || !bgra) return;

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint32_t* p = vbePixelAddr((int)x + (int)col, (int)y + (int)row);
            if (!p) continue;

            const uint8_t* src = &bgra[((size_t)row * w + col) * 4];
            uint32_t b = src[0];
            uint32_t g = src[1];
            uint32_t r = src[2];
            uint32_t a = src[3];

            if (a == 0) continue;
            if (a == 255) {
                *p = (r << 16) | (g << 8) | b;
                continue;
            }

            /* Alpha 混合：dst = (src*a + dst*(255-a)) / 255 */
            uint32_t invA = 255 - a;
            uint32_t dr = (*p >> 16) & 0xFF;
            uint32_t dg = (*p >> 8)  & 0xFF;
            uint32_t db =  *p        & 0xFF;
            *p = (((r * a + dr * invA) / 255) << 16)
               | (((g * a + dg * invA) / 255) << 8)
               | ((b * a + db * invA) / 255);
        }
    }
}

/* 用当前绘制色填充椭圆。椭圆内接于 (x,y) 为左上角、宽 w、高 h 的包围盒，
 * 中心 = (x+w/2, y+h/2)，横半轴 = w/2，纵半轴 = h/2。
 * 对包围盒内每个像素做椭圆方程测试：(dx/rx)^2 + (dy/ry)^2 <= 1 即填充，
 * 从而一次铺满内部，而非只描轮廓。 */
void vbeDrawFillEllipse(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    float rx = (float)w * 0.5f;      /* 横半轴 */
    float ry = (float)h * 0.5f;      /* 纵半轴 */
    float cx = (float)x + rx;        /* 圆心 x */
    float cy = (float)y + ry;        /* 圆心 y */
    float rx2 = rx * rx;
    float ry2 = ry * ry;

    for (uint32_t py = y; py < y + h; py++) {
        float dy = (float)py - cy;
        for (uint32_t px = x; px < x + w; px++) {
            float dx = (float)px - cx;
            if ((dx * dx) / rx2 + (dy * dy) / ry2 <= 1.0f)
                vbeDrawPixel(px, py);   /* vbeDrawPixel 内部做边界检查 */
        }
    }
}

/* 用当前绘制色描边椭圆（只画轮廓，内部不填充）。
 * 椭圆内接于 (x,y) 为左上角、宽 w、高 h 的包围盒，
 * 中心 = (x+w/2, y+h/2)，横半轴 = w/2，纵半轴 = h/2。
 * 判断用椭圆方程测试，并把边界稍向外扩张一点，使轮廓在整数格上连续不出现空洞。 */
void vbeDrawLineEllipse(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    float rx = (float)w * 0.5f;      /* 横半轴 */
    float ry = (float)h * 0.5f;      /* 纵半轴 */
    float cx = (float)x + rx;        /* 圆心 x */
    float cy = (float)y + ry;        /* 圆心 y */
    float rx2 = rx * rx;
    float ry2 = ry * ry;

    for (uint32_t py = y; py < y + h; py++) {
        float dy = (float)py - cy;
        for (uint32_t px = x; px < x + w; px++) {
            float dx = (float)px - cx;
            float d  = (dx * dx) / rx2 + (dy * dy) / ry2;
            if (d >= 0.9f && d <= 1.1f)   /* 落在轮廓带内即描边 */
                vbeDrawPixel(px, py);     /* vbeDrawPixel 内部做边界检查 */
        }
    }
}

void vbeDrawLine(uint32_t x, uint32_t y, uint32_t x2, uint32_t y2) {
    // 参数说明：
    // x, y   - 起点坐标
    // x2, y2 - 终点坐标
    // w, h   - 屏幕宽度和高度（用于边界检查）

    // 计算x和y的差值
    int32_t dx = (int32_t)x2 - (int32_t)x;
    int32_t dy = (int32_t)y2 - (int32_t)y;

    // 确定步进方向
    int32_t step_x = (dx > 0) ? 1 : -1;
    int32_t step_y = (dy > 0) ? 1 : -1;

    // 取绝对值
    dx = (dx > 0) ? dx : -dx;
    dy = (dy > 0) ? dy : -dy;

    // 当前坐标
    uint32_t current_x = x;
    uint32_t current_y = y;

    // 绘制第一个像素
    if (current_x < vbeWidth && current_y < vbeHeight) {
        vbeDrawPixel(current_x, current_y);
    }

    // 判断直线是"陡峭"还是"平缓"
    if (dx > dy) {
        // 平缓直线：x方向变化快
        // 误差累积器
        int32_t error = dx / 2;

        for (int32_t i = 0; i < dx; i++) {
            current_x += step_x;
            error -= dy;

            if (error < 0) {
                current_y += step_y;
                error += dx;
            }

            // 边界检查后绘制
            if (current_x < vbeWidth && current_y < vbeHeight) {
                vbeDrawPixel(current_x, current_y);
            }
        }
    } else {
        // 陡峭直线：y方向变化快
        int32_t error = dy / 2;

        for (int32_t i = 0; i < dy; i++) {
            current_y += step_y;
            error -= dx;

            if (error < 0) {
                current_x += step_x;
                error += dy;
            }

            // 边界检查后绘制
            if (current_x < vbeWidth && current_y < vbeHeight) {
                vbeDrawPixel(current_x, current_y);
            }
        }
    }
}

/* 注入 8x16 字形：font.bin 每字符 17 字节 = [码][16 行]。码升序或乱序均可。 */
void vbeLoadFont(const uint8_t* data, uint32_t size) {
    if (!data) return;
    bool got = false;
    for (uint32_t i = 0; i + 17 <= size && i < 256 * 17; i += 17) {
        uint8_t code = data[i];
        for (uint8_t r = 0; r < 16; r++) sFontGlyph[code][r] = data[i + 1 + r];
        got = true;
    }
    if (got) sFont8x16 = true;
}

/* 在 (x, y) 处用当前绘制色绘制单个字符，返回该字符的横向步进(像素)。
 * 注入 8x16 字体时用 8x16 渲染，否则回退到内置 8x8。 */
uint16_t vbeDrawChar(uint16_t x, uint16_t y, char c) {
    if (!vbeRenderable()) return VBE_FONT_W;

    const uint8_t charH = sFont8x16 ? 16 : 8;

    uint8_t rows[16];
    if (sFont8x16) {
        for (uint8_t i = 0; i < 16; i++) rows[i] = sFontGlyph[(uint8_t)c][i];
    } else {
        uint8_t idx = (uint8_t)(c - 0x20);
        if (idx >= 95) idx = 0;               /* 超出字体表显示为空白 */
        for (uint8_t i = 0; i < 8; i++)  rows[i] = sFont8x8[idx][i];
        for (uint8_t i = 8; i < 16; i++) rows[i] = 0;
    }

    for (uint8_t row = 0; row < charH; row++) {
        uint8_t bits = rows[row];
        for (uint8_t col = 0; col < VBE_FONT_W; col++) {
            if (!(bits & (0x80 >> col))) continue;
            uint32_t* p = vbePixelAddr((int)x + col, (int)y + row);
            if (p) *p = sColor;
        }
    }
    return VBE_FONT_W;
}

/* 图形文本渲染：逐字符调用 vbeDrawChar，支持 '\n' 换行与 '\r' 回车。
 * 使用当前绘制色。 */
void vbeDrawString(uint16_t x, uint16_t y, const char* str) {
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32 || !str) return;

    const uint8_t charH = sFont8x16 ? 16 : 8;
    uint16_t      px    = x;
    uint16_t      py    = y;

    for (; *str; str++) {
        if (*str == '\n') {
            py += charH;
            px = x;
            continue;
        }
        if (*str == '\r') {
            px = x;
            continue;
        }
        px += vbeDrawChar(px, py, *str);
    }
}

/* ==================== CJK(中文)显示支持 ==================== */

#define CJK_REC_SIZE  36          /* 每条记录: [码点_LE4][32 字节点阵] */
#define CJK_GLYPH_OFF 4           /* 码点之后即为 32 字节点阵 */
#define CJK_GLYPH_LEN 32

/* 注入 16x16 CJK 点阵字库：cjk16.bin 每条记录 36 字节
 * (码点升序)。内部复制保存，调用方释放原缓冲不影响使用；已加载则替换。 */
void vbeLoadCjkFont(const uint8_t* data, uint32_t size) {
    if (!data || size < CJK_REC_SIZE) return;
    uint32_t count = size / CJK_REC_SIZE;
    uint32_t bytes = count * CJK_REC_SIZE;

    if (bytes > sCjkCapacity) {
        uint8_t* nb = malloc(bytes);
        if (!nb) return;
        if (sCjkGlyphs) free(sCjkGlyphs);
        sCjkGlyphs = nb;
        sCjkCapacity = bytes;
    }
    memcpy(sCjkGlyphs, data, bytes);
    sCjkCount = count;
}

/* 在 CJK 字库中二分查找码点，命中返回指向其 16x16 点阵的指针，否则 NULL。
 * 依赖字形按码点升序存储(由 cjk16.bin 生成时保证)。 */
static const uint8_t* cjkFindGlyph(uint32_t code) {
    if (!sCjkGlyphs || sCjkCount == 0) return NULL;

    uint32_t lo = 0, hi = sCjkCount;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        const uint8_t* rec = sCjkGlyphs + mid * CJK_REC_SIZE;
        uint32_t cp = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8)
                    | ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
        if (cp < code)       lo = mid + 1;
        else if (cp > code)  hi = mid;
        else return rec + CJK_GLYPH_OFF;
    }
    return NULL;
}

/* 解码 UTF-8 序列：str 指向首字节，len 为后续参与解码的剩余长度。
 * 返回解码后的码点；非法序列返回 (uint32_t)-1。 */
static uint32_t utf8Decode(const uint8_t* s, uint32_t len) {
    uint8_t b0 = s[0];
    uint32_t cp;
    uint32_t need;
    if (b0 < 0x80) { return b0; }
    else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; need = 1; }
    else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; need = 2; }
    else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; need = 3; }
    else return (uint32_t)-1;

    if (len < need) return (uint32_t)-1;   /* 序列不完整 */
    for (uint32_t i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) return (uint32_t)-1;   /* 续字节非法 */
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    return cp;
}

/* 在 (x,y) 用当前绘制色渲染单个全宽汉字，字形为内嵌 16 字节*2 的 16x16 点阵。
 * 返回该字符的横向步进(16 像素)。
 * Unifont 的汉字字形为满格 16×16(内容占满 0..15 行)，与 ASCII 的 8×16 同一
 * 网格坐标系、顶部对齐，故无需偏移(UPDAWN=0)。改换其它字库(内容非顶格)时
 * 再用该宏把整幅字形整体上移对齐，顶部越界的行自动裁剪、内容不丢弃。 */
#define CJK_GLYPH_UPDAWN 0

static uint16_t vbeDrawCjkGlyph(uint16_t x, uint16_t y, const uint8_t* glyph) {
    if (!vbeRenderable() || !glyph) return 16;

    for (uint16_t row = 0; row < 16; row++) {
        /* 整幅字形上移 CJK_GLYPH_UPDAWN 像素；顶部越界的行(负数)跳过 */
        int yy = (int)y + row - CJK_GLYPH_UPDAWN;
        if (yy < 0) continue;
        uint8_t hi = glyph[row * 2];
        uint8_t lo = glyph[row * 2 + 1];
        for (uint16_t col = 0; col < 16; col++) {
            uint8_t bit = (col < 8) ? (uint8_t)(0x80 >> col)
                                    : (uint8_t)(0x80 >> (col - 8));
            uint8_t src = (col < 8) ? hi : lo;
            if (!(src & bit)) continue;
            uint32_t* p = vbePixelAddr((int)x + col, yy);
            if (p) *p = sColor;
        }
    }
    return 16;
}

/* 识别 UTF-8 字符串并渲染：ASCII(<0x80) 用 8x16 字体，
 * 多字节字符若能匹配 CJK 字库则以 16x16 双宽渲染，否则跳过。
 * 支持 '\n' 换行、'\r' 回车。使用当前绘制色。 */
void vbeDrawStringCJK(uint16_t x, uint16_t y, const char* str) {
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32 || !str) return;

    const uint8_t charH  = sFont8x16 ? 16 : 8;
    const char*   p      = str;
    uint16_t      px     = x;
    uint16_t      py     = y;

    while (*p) {
        uint8_t b0 = (uint8_t)*p;

        if (b0 == '\n') { py += charH; px = x; p++; continue; }
        if (b0 == '\r') { px = x; p++; continue; }

        /* 判断长度并解码多字节字符 */
        if (b0 >= 0x80) {
            uint32_t need = 0;
            if ((b0 & 0xE0) == 0xC0) need = 2;
            else if ((b0 & 0xF0) == 0xE0) need = 3;
            else if ((b0 & 0xF8) == 0xF0) need = 4;

            if (need) {
                const uint8_t* rest = (const uint8_t*)p;
                uint32_t cp = utf8Decode(rest, (uint32_t)(strlen(p)));
                if (cp != (uint32_t)-1) {
                    const uint8_t* glyph = cjkFindGlyph(cp);
                    if (glyph) {
                        px += vbeDrawCjkGlyph(px, py, glyph);
                        p += (int)need;
                        continue;
                    }
                }
                /* 未匹配 CJK 字库：跳过该多字节序列(占一字符宽) */
                p += (int)need;
                px += 16;
                continue;
            }
        }

        /* 普通 ASCII */
        px += vbeDrawChar(px, py, *p);
        p++;
    }
}