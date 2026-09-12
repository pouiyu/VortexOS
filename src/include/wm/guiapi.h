#ifndef _WM_GUIAPI_H
#define _WM_GUIAPI_H

/* 用户 GUI 程序与内核 WM 服务之间的 ABI：
 * - 系统调用经 int 0x80，EAX=调用号，EBX/ECX/EDX=参数。
 * - 身份映射(用户/内核同一地址空间)，内核可直接访问用户传入的指针。
 * - 本头同时被内核(wmsvc.c)与用户程序(libgui.c/gui_demo.c)包含。 */

#include <stdint.h>
#include <wm/window.h>   /* WM_STYLE_* 等窗口样式位 */

/* 系统调用号(基本 I/O 沿袭旧编号) */
#define SYS_READ      1
#define SYS_WRITE     2
#define SYS_EXIT      3

/* 窗口管理器(W)系统调用组 */
#define SYS_WM_CREATE        10  /* ebx=WmCreateArgs* → 返回窗口索引(-1 失败) */
#define SYS_WM_SET_COLOR     11  /* ebx=index, ecx=color */
#define SYS_WM_FILL_RECT     12  /* ebx=index, ecx=cx|cy<<16, edx=cw|ch<<16 */
#define SYS_WM_DRAW_TEXT     13  /* ebx=index, ecx=cx|cy<<16, edx=text* */
#define SYS_WM_DRAW_LINE     14  /* ebx=index, ecx=x0|y0<<16, edx=x1|y1<<16 */
#define SYS_WM_DRAW_LINE_RECT 15 /* ebx=index, ecx=cx|cy<<16, edx=cw|ch<<16 */
#define SYS_WM_SET_TITLE     16  /* ebx=index, ecx=title* */
#define SYS_WM_CLOSE         17  /* ebx=index */
#define SYS_WM_FLUSH         18  /* 无参: 合成+写 LFB+指针 */
#define SYS_WM_POLL          19  /* ebx=WmEvent*: 等待输入并回填事件 */
#define SYS_WM_GET_POS       20  /* ebx=index, ecx=WmPos*: 读窗口左上角(可拖动变化) */

/* 鼠标按钮位(与驱动一致, 供用户程序判定) */
#define GUI_MOUSE_LEFT   0x01
#define GUI_MOUSE_RIGHT  0x02
#define GUI_MOUSE_MIDDLE 0x04

/* SYS_WM_CREATE 参数 */
typedef struct {
    const char* title;
    int  x, y, w, h;
    uint32_t titleColor;
    uint32_t bodyColor;
    uint32_t style;
} WmCreateArgs;

/* SYS_WM_POLL 事件回填 */
typedef struct {
    int32_t  mouseX, mouseY;   /* 鼠标绝对坐标 */
    uint8_t  buttons;          /* GUI_MOUSE_* 位组合(当前状态) */
    uint8_t  leftDown;         /* 左键当前按下=1 */
    char     key;              /* 0=无按键, 否则键盘字符(含 KEY_ESC 等) */
    uint8_t  flags;            /* 保留 */
} WmEvent;

/* SYS_WM_GET_POS 回填 */
typedef struct {
    int32_t x, y;              /* 窗口左上角(屏幕坐标) */
} WmPos;

#endif
