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

/* 解锁/使能控制字（值域，不对外暴露） */
#define VBE_ID_LCK        0xB0C1   /* 写入该 ID 可解锁 VBE 寄存器 */
#define VBE_ID_MASK       0xFFF0   /* 版本号高 12 位匹配依据 */
#define VBE_ID_BASE       0xB0C0   /* 最小的合法版本号 */
#define VBE_CMD_ENABLE    0x01
#define VBE_CMD_LFB       0x40

VbeInfo gVbeInfo;
static bool sReady = false;

/* 8x16 字形表：由 vbeLoadFont() 从 font.bin(每字符17字节: 码+16行)注入，
 * 供 vbeDrawString() 使用。未注入时回退到内置 8x8 点阵。 */
static uint8_t sFontGlyph[256][16];
static bool    sFont8x16 = false;

/* 当前绘制色：图形模式没有前景/背景色之分，先 vbeSetColor 设置再绘制，
 * 所有绘图/文本函数均使用该颜色。32bpp 为 0x00RRGGBB。 */
static uint32_t sColor = 0x00FFFFFF;  /* 默认白色 */

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

void vbeSyncInfo(void) {
    if (!sReady) vbeInit();
    /* 显存以 64K 为单位，任何状态都可读 */
    gVbeInfo.vramSize = (uint32_t)vbeRead(VBE_DISPI_MEM64K) * 65536u;
    /* 分辨率/色深仅在图形(LFB)模式下有意义 */
    if (gVbeInfo.enabled) {
        gVbeInfo.xres = vbeRead(VBE_DISPI_XRES);
        gVbeInfo.yres = vbeRead(VBE_DISPI_YRES);
        gVbeInfo.bpp  = vbeRead(VBE_DISPI_BPP);
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
    if (!vbeReady() && vbeInit() != 0) return -1;

    /* 顺序：宽度 → 高度 → 色深 → 使能(带 LFB) */
    vbeWrite(VBE_DISPI_XRES, xres);
    vbeWrite(VBE_DISPI_YRES, yres);
    vbeWrite(VBE_DISPI_BPP,  bpp);
    vbeWrite(VBE_DISPI_ENABLE, VBE_CMD_ENABLE | VBE_CMD_LFB);

    gVbeInfo.xres = vbeRead(VBE_DISPI_XRES);
    gVbeInfo.yres = vbeRead(VBE_DISPI_YRES);
    gVbeInfo.bpp  = vbeRead(VBE_DISPI_BPP);
    gVbeInfo.enabled = 1;

    vbeMapLfb();
    return 0;
}

void vbeDisable(void) {
    vbeWrite(VBE_DISPI_ENABLE, 0);   /* 回到文本模式 */
    vbeForceTextMode();              /* 显式恢复 80x25 文本模式，弥补个别固件不回文本的情况 */
    gVbeInfo.enabled = 0;
}

/* 用当前绘制色清屏 */
void vbeClearScreen(void) {
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32) return;
    uint32_t* fb = (uint32_t*)gVbeInfo.lfbAddr;
    uint32_t n = gVbeInfo.xres * gVbeInfo.yres;
    for (uint32_t i = 0; i < n; i++) fb[i] = sColor;
}

/* 用当前绘制色绘制单个像素 */
void vbeDrawPixel(uint32_t x, uint32_t y) {
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32) return;
    if (x >= gVbeInfo.xres || y >= gVbeInfo.yres) return;
    ((uint32_t*)gVbeInfo.lfbAddr)[y * gVbeInfo.xres + x] = sColor;
}

/* 用当前绘制色填充矩形 */
void vbeDrawFillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    for (uint32_t y = y; y < y + h && y < gVbeInfo.yres; y++) {
        for (uint32_t x = x; x < x + w && x < gVbeInfo.xres; x++) {
            vbeDrawPixel(x, y);
        }
    }
}

void vbeDrawLineRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    vbeDrawLine(x,y,x+w,y);
    vbeDrawLine(x,y,x,y+h);
    vbeDrawLine(x+w,y,x+w,y+h);
    vbeDrawLine(x,y+h,x+w,y+h);
}

/* 把自顶向下、BGRA(蓝|绿|红|alpha) 的像素缓冲按 Alpha 混合绘制到帧缓冲。
 * 越界区域自动裁剪；alpha 为 0 跳过、255 直接覆盖、其余按比例混合。
 * bgra 一行紧接着一行，每像素 4 字节。 */
void vbeDrawBitmap(uint32_t x, uint32_t y, const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32 || !bgra) return;

    uint32_t* fb = (uint32_t*)gVbeInfo.lfbAddr;
    for (uint32_t row = 0; row < h; row++) {
        uint32_t yy = y + row;
        if (yy >= gVbeInfo.yres) break;
        for (uint32_t col = 0; col < w; col++) {
            uint32_t xx = x + col;
            if (xx >= gVbeInfo.xres) break;

            const uint8_t* p = &bgra[((size_t)row * w + col) * 4];
            uint32_t b = p[0];
            uint32_t g = p[1];
            uint32_t r = p[2];
            uint32_t a = p[3];

            size_t idx = (size_t)yy * gVbeInfo.xres + xx;
            if (a == 0) {
                continue;
            }
            if (a == 255) {
                fb[idx] = (r << 16) | (g << 8) | b;
                continue;
            }

            /* Alpha 混合：dst = (src*a + dst*(255-a)) / 255 */
            uint32_t invA = 255 - a;
            uint32_t dr = (fb[idx] >> 16) & 0xFF;
            uint32_t dg = (fb[idx] >> 8)  & 0xFF;
            uint32_t db =  fb[idx]        & 0xFF;
            fb[idx] = (((r * a + dr * invA) / 255) << 16)
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
    if (!gVbeInfo.enabled || gVbeInfo.bpp != 32) return VBE_FONT_W;

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

    uint32_t* fb = (uint32_t*)gVbeInfo.lfbAddr;
    for (uint8_t row = 0; row < charH; row++) {
        uint8_t  bits = rows[row];
        uint16_t yy   = y + row;
        if (yy >= gVbeInfo.yres) break;
        for (uint8_t col = 0; col < VBE_FONT_W; col++) {
            uint16_t xx = x + col;
            if (xx >= gVbeInfo.xres) break;
            if (bits & (0x80 >> col))
                fb[yy * gVbeInfo.xres + xx] = sColor;
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