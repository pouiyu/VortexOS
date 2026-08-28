// bmp.c
// BMP 位图文件解码：从文件系统读入文件，解析 24/32bpp 未压缩(BI_RGB) 位图，
// 输出自顶向下、BGRA(含 alpha) 的像素缓冲，供 vbeDrawBitmap() 绘制。
#include "bmp.h"
#include <fs/file.h>
#include <stdlib/stdlib.h>

/* BMP 文件头字段偏移 */
#define BMP_OFF_MAGIC     0   /* 2 字节: 'B','M' */
#define BMP_OFF_OFFBITS   10  /* 4 字节: 像素数据起始偏移 */
#define BMP_OFF_WIDTH     18  /* 4 字节: 宽度(可负) */
#define BMP_OFF_HEIGHT    22  /* 4 字节: 高度(负=自顶向下) */
#define BMP_OFF_PLANES    26  /* 2 字节: 平面数(须为 1) */
#define BMP_OFF_BPP       28  /* 2 字节: 每像素位数 */
#define BMP_OFF_COMPRESS  30  /* 4 字节: 压缩方式(0=BI_RGB) */

int bmpLoad(const char* path, BmpImage* out) {
    out->pixels = NULL;
    out->w = 0;
    out->h = 0;
    out->loaded = false;

    FileHandle f;
    if (!fsOpen(&f, path)) return -1;
    if (f.size < 54) {                /* 连文件头都不够 */
        fsClose(&f);
        return -2;
    }

    /* 整个文件读入内存再解析 */
    uint8_t* raw = malloc((size_t)f.size);
    if (!raw) {
        fsClose(&f);
        return -3;
    }
    if (fsRead(&f, raw, f.size) != f.size) {
        fsClose(&f);
        free(raw);
        return -4;
    }
    fsClose(&f);

    /* 校验签名 */
    if (raw[BMP_OFF_MAGIC] != 'B' || raw[BMP_OFF_MAGIC + 1] != 'M') {
        free(raw);
        return -5;
    }

    uint32_t offBits = *(uint32_t*)(raw + BMP_OFF_OFFBITS);
    int32_t  sWidth  = *(int32_t*)(raw + BMP_OFF_WIDTH);
    int32_t  sHeight = *(int32_t*)(raw + BMP_OFF_HEIGHT);
    uint16_t planes  = *(uint16_t*)(raw + BMP_OFF_PLANES);
    uint16_t bpp     = *(uint16_t*)(raw + BMP_OFF_BPP);
    uint32_t compress = *(uint32_t*)(raw + BMP_OFF_COMPRESS);

    /* 只支持未压缩(BI_RGB)与带位域的 32/24bpp(BI_BITFIELDS, 标准 RGBA 掩码时字节序同为 BGR)；
       平面数须为 1；bpp 只支持 24/32 */
    if ((compress != 0 && compress != 3) || planes != 1 || (bpp != 24 && bpp != 32)) {
        free(raw);
        return -6;
    }
    if (sWidth <= 0 || sHeight == 0) {
        free(raw);
        return -7;
    }

    /* 高度为正=自底向上存储(需翻转)，为负=自顶向下 */
    bool topDown = (sHeight < 0);
    uint32_t w   = (uint32_t)sWidth;
    uint32_t h   = (uint32_t)(topDown ? -sHeight : sHeight);

    uint32_t bytesPerPix = bpp / 8;
    /* 每行按 4 字节对齐 */
    uint32_t rowSize = ((w * bpp + 31) / 32) * 4;
    if ((uint64_t)offBits + (size_t)rowSize * h > (size_t)f.size) {
        free(raw);
        return -8;
    }

    /* 目标缓冲：BGRA，自顶向下 */
    uint8_t* bgra = malloc((size_t)w * h * 4);
    if (!bgra) {
        free(raw);
        return -9;
    }

    for (uint32_t y = 0; y < h; y++) {
        uint32_t srcRow = topDown ? y : (h - 1 - y);   /* 自底向上则反序取行 */
        const uint8_t* src = raw + offBits + (size_t)srcRow * rowSize;
        uint8_t* dst = bgra + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* p = src + (size_t)x * bytesPerPix;
            dst[x * 4 + 0] = p[0];               /* B */
            dst[x * 4 + 1] = p[1];               /* G */
            dst[x * 4 + 2] = p[2];               /* R */
            dst[x * 4 + 3] = (bpp == 32) ? p[3] : 255;  /* A */
        }
    }

    free(raw);
    out->pixels = bgra;
    out->w = w;
    out->h = h;
    out->loaded = true;
    return 0;
}

void bmpFree(BmpImage* img) {
    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
    img->w = 0;
    img->h = 0;
    img->loaded = false;
}