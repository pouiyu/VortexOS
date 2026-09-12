#ifndef _GUI_LIBGUI_H
#define _GUI_LIBGUI_H

/* 用户态 GUI 库：封装 SYS_WM_* 系统调用(经 int 0x80)。
 * 供独立编译的用户 GUI 程序(ELF)使用，不链接内核。
 * 身份映射下内核可直接访问本程序传出的指针，字符串参数只需指向本程序内存。
 *
 * 使用流程：
 *   guiCreateWindow 创建窗口 → guiSetColor/guiFillRect/guiDrawText...
 *   绘制客户区 → guiFlush 上屏 → guiPoll 阻塞取输入 → ESC 等条件满足时
 *   guiExit 返回内核(恢复文本主菜单)。
 * 窗口的拖动/聚焦/最小化/关闭由内核 WM 服务在 guiPoll 期间自动处理。 */

#include <stdint.h>
#include <wm/guiapi.h>
#include <wm/window.h>   /* WM_STYLE_*、WM_TITLEBAR_H */

/* 32bpp 颜色 = 0x00RRGGBB。宏以便用于静态常量初始化。 */
#define guiRgb(r, g, b) \
    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* 创建窗口，返回窗口索引(-1 失败)。style 传 WM_STYLE_* 位组合，
 * 常规窗口用 WM_STYLE_DEFAULT。客户区绘制坐标以客户区左上角为原点。 */
int guiCreateWindow(const char* title, int x, int y, int w, int h,
                    uint32_t titleColor, uint32_t bodyColor, uint32_t style);

/* 设置窗口客户区当前绘制色(单一绘制色模型，先设色再绘制)。 */
void guiSetColor(int win, uint32_t color);

/* 客户区相对坐标绘制命令(越界自动裁剪)。 */
void guiFillRect(int win, int cx, int cy, int cw, int ch);      /* 用当前色填充矩形 */
void guiDrawText(int win, int cx, int cy, const char* text);    /* 用当前色画文本(CJK) */
void guiDrawLine(int win, int x0, int y0, int x1, int y1);      /* 用当前色画直线 */
void guiDrawLineRect(int win, int cx, int cy, int cw, int ch);  /* 用当前色画线框矩形 */

/* 修改窗口标题。 */
void guiSetTitle(int win, const char* title);

/* 读窗口当前左上角(屏幕坐标)：窗口可被拖动，命中检测需用它换算客户区坐标。 */
void guiGetWinPos(int win, int* x, int* y);

/* 关闭窗口(隐藏)。 */
void guiClose(int win);

/* 把当前所有窗口客户区 surface 合成上屏(合成+写 LFB+鼠标指针)。 */
void guiFlush(void);

/* 阻塞等待一次输入(鼠标/键盘)，期间内核处理拖拽/聚焦/最小化/关闭。
 * 返回后 WmEvent 含最新鼠标坐标、按钮状态与键盘字符。 */
void guiPoll(WmEvent* ev);

/* 退出 GUI 程序(SYS_EXIT)：恢复内核上下文，返回文本主菜单。 */
void guiExit(void);

#endif
