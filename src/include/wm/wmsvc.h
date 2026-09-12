#ifndef _WM_WMSVC_H
#define _WM_WMSVC_H

/* 内核 WM 服务：为"用户态 GUI 程序经 syscall 创建窗口/绘制 UI"提供服务。
 * 拥有：输入事件循环(拖拽/关闭/最小化/聚焦)、合成+写 LFB、鼠标指针绘制。
 * 用户程序只负责：创建窗口 → 往客户区 surface 绘制 → guiFlush 上屏 →
 * guiPoll 取输入。 */

#include <stdint.h>
#include <stddef.h>
#include <wm/guiapi.h>

/* 注册渲染缓冲与屏幕尺寸。shadow 与 dragBack 均为 width*height*4 的 RAM 缓冲，
 * 由调用方(graphic_main)长期持有并保证与当前分辨率匹配。 */
void wmsvcInit(uint32_t* shadow, uint32_t* dragBack, size_t bgPix);

/* 注册鼠标箭头位图(BGRA, 自顶向下)；NULL 时回退内置白方块指针。 */
void wmsvcSetArrow(const uint8_t* pixels, uint32_t w, uint32_t h);

/* 处理一个 WM 系统调用(SYS_WM_*)。fn 为调用号，a/b/c 对应 EBX/ECX/EDX。
 * 返回给用户程序的整型返回值。 */
int wmsvcHandle(uint32_t fn, uint32_t a, uint32_t b, uint32_t c);

#endif
