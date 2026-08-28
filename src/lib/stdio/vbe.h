#ifndef _VBE_H
#define _VBE_H

#include <stdint.h>
#include <stdbool.h>

/* Bochs/QEMU VBE(dispi) 硬件接口：通过 I/O 端口访问，提供线性帧缓冲(LFB)图形模式 */

/* VBE dispi I/O 端口 */
#define VBE_INDEX_PORT 0x01CE
#define VBE_DATA_PORT  0x01CF

/* dispi 寄存器索引 */
#define VBE_DISPI_ID           0x0   /* 版本/解锁 ID */
#define VBE_DISPI_XRES         0x1   /* 宽度 */
#define VBE_DISPI_YRES         0x2   /* 高度 */
#define VBE_DISPI_BPP          0x3   /* 色深 */
#define VBE_DISPI_ENABLE       0x4   /* 使能/控制 */
#define VBE_DISPI_MEM64K       0xA   /* 显存大小(以 64K 为单位) */

/* 显示器/帧缓冲信息 */
typedef struct {
    uint32_t lfbAddr;   /* 线性帧缓冲物理地址 */
    uint32_t vramSize;  /* 显存大小(字节) */
    uint16_t xres;      /* 当前逻辑宽度 */
    uint16_t yres;      /* 当前逻辑高度 */
    uint16_t bpp;       /* 当前色深 */
    uint8_t  enabled;   /* 是否已进入图形(LFB)模式 */
} VbeInfo;

int  vbeInit(void);                       /* PCI 探测显卡并解锁 VBE，0=成功 */
bool vbeReady(void);                      /* 是否已初始化 */
void vbeSyncInfo(void);                   /* 回填显存与当前模式到 gVbeInfo */
int  vbeSetMode(uint16_t xres, uint16_t yres, uint16_t bpp); /* 进入图形模式 */
void vbeDisable(void);                    /* 关闭图形(LFB)模式，返回文本模式 */

/* 颜色状态：图形模式没有前景/背景色之分，先 vbeSetColor 设置当前绘制色，
 * 之后所有绘图/文本函数均使用该颜色。32bpp 颜色为 0x00RRGGBB。 */
void     vbeSetColor(uint32_t color);              /* 设置当前绘制色 */
void     vbeSetColorRgb(uint8_t r, uint8_t g, uint8_t b); /* 当前绘制色 = vbeColor(r,g,b) */
uint32_t vbeGetColor(void);                          /* 返回当前绘制色 */

/* 帧缓冲绘图（仅 32bpp 生效）。所有函数使用当前绘制色。 */
void vbeClearScreen(void);                                     /* 用当前绘制色清屏 */
void vbeDrawPixel(uint32_t x, uint32_t y);                     /* 单个像素 */
void vbeDrawFillRect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h); /* 填充矩形 */
void vbeDrawFillEllipse(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* 填充椭圆 */
void vbeDrawLineEllipse(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* 描边椭圆 */
void vbeDrawLineRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* 线框矩形 */
void vbeDrawLine(uint32_t x, uint32_t y, uint32_t x2, uint32_t y2); /* 直线 */

/* 位图：把自顶向下、BGRA(含 alpha) 像素缓冲按 Alpha 混合绘制，越界自动裁剪。
 * 与 bmp.c 的 bmpLoad() 输出格式一致。 */
void vbeDrawBitmap(uint32_t x, uint32_t y, const uint8_t* bgra, uint32_t w, uint32_t h);

/* 图形文本：位图字体按行渲染（支持 '\n' 换行）。使用当前绘制色。
 * 注入 8x16 字体后按 8x16 渲染，否则回退内置 8x8。 */
uint16_t vbeDrawChar(uint16_t x, uint16_t y, char ch);         /* 单个字符，返回步进 */
void vbeDrawString(uint16_t x, uint16_t y, const char* str);   /* 字符串 */

/* 注入 8x16 字形：font.bin 每字符 17 字节 = [码][16 行] */
void vbeLoadFont(const uint8_t* data, uint32_t size);

static inline uint32_t vbeColor(uint8_t r, uint8_t g, uint8_t b) {
    // 最常见的是 0x00RRGGBB
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

extern VbeInfo gVbeInfo;
extern int vbeWidth;
extern int vbeHeight;

#define VBE_FONT_W 8   /* 单个字符宽度(像素) */
#define VBE_FONT_H 16  /* 单个字符宽度(像素) */

#endif