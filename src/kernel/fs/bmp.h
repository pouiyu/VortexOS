#ifndef _KERNEL_FS_BMP_H
#define _KERNEL_FS_BMP_H

#include <stdint.h>
#include <stdbool.h>

/* 解码后的位图。
 * pixels 为自顶向下(第 0 行为图片最顶行)、BGRA(蓝|绿|红|alpha) 顺序，
 * 共 w*h*4 字节；供 vbeDrawBitmap() 直接绘制。 */
typedef struct {
    uint8_t* pixels;
    uint32_t w;
    uint32_t h;
    bool     loaded;
} BmpImage;

/* 从文件系统加载 BMP 并解码到 out。成功返回 0，失败返回负值。
 * 支持未压缩(BI_RGB) 的 24/32bpp；32bpp 的第 4 字节作为 alpha 通道。 */
int  bmpLoad(const char* path, BmpImage* out);
void bmpFree(BmpImage* img);

#endif