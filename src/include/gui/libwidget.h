#ifndef _GUI_LIBWIDGET_H
#define _GUI_LIBWIDGET_H
/* libwidget —— 构建在 libgui 之上的一组轻量 UI 组件(按钮/标签/文本框/复选/直线/线框)。
 * 面向"可视化设计器生成代码"场景：统一用 Widget 数组描述一组控件，
 * 提供一次性绘制(wdDraw)、几何命中(wdHit)、以及简单交互(按钮按下/复选切换/
 * 文本框 ASCII 输入)。本库无堆分配；文本框输入缓冲须由调用方提供静态数组。
 *
 * 依赖：<gui/libgui.h>、<wm/window.h>(WM_TITLEBAR_H)、<keyboard.h>(KEY_*)。 */

#include <stdint.h>
#include <gui/libgui.h>
#include <wm/window.h>
#include <keyboard.h>

/* 控件类型 */
typedef enum {
    WD_BUTTON = 0,   /* 按钮: 填充 bg, 按下换 fg, 白字 label 居中 */
    WD_LABEL  = 1,   /* 标签: 用 fg 画 label */
    WD_TEXTBOX= 2,   /* 文本框: 白底 + border 线框 + buf 内文本(可聚焦输入) */
    WD_CHECKBOX=3,   /* 复选: 方块 + 对勾 + label */
    WD_LINE   = 4,   /* 直线: 按 w/h 长边方向画分隔线(用 border 色) */
    WD_RECT   = 5    /* 线框矩形: 用 border 色画外框 */
} WdType;

/* 统一组件描述。x,y 为窗口客户区相对坐标。
 * TextBox 的 buf 指向调用方静态数组, cap 为容量(含结尾 NUL), len 为当前长度。 */
typedef struct {
    WdType type;
    int x, y, w, h;          /* 客户区相对坐标与尺寸 */
    const char* label;       /* 显示的文本(标签/按钮/复选) */
    uint32_t fg, bg, border; /* 前景 / 背景 / 边框色 */

    /* 运行时状态 */
    int down;                /* Button: 当前是否按下 */
    char* buf; int cap;      /* TextBox: 输入缓冲 + 容量 */
    int len;                 /* TextBox: 当前字节数 */
    int checked;             /* CheckBox: 是否勾选 */
} Widget;

/* 复位文本相关状态：TextBox 把 label(初始值)拷贝进 buf 并置 len。 */
void wdInit(Widget* ws, int n);

/* 依序绘制 n 个控件到窗口 win 的客户区(全部用 libgui 原语)。 */
void wdDraw(int win, Widget* ws, int n);

/* 纯几何命中：返回包含点(mx,my)(客户区坐标)的控件索引, 未命中返回 -1。
 * LINE/RECT 按外接矩形参与命中(便于选中)。 */
int  wdHit(const Widget* ws, int n, int mx, int my);

/* 交互：按钮按下状态更新(按下换色)、复选点击切换。返回是否需要重绘。 */
int  wdPress(int win, Widget* ws, int n, int idx, int down);

/* 键盘输入处理：当 *focus 指向文本框时, 可打印 ASCII append、退格删尾。
 * 返回是否有变更(需要重绘)。 */
int  wdHandleKey(int win, Widget* ws, int n, int* focus, char key);

#endif /* _GUI_LIBWIDGET_H */