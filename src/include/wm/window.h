#ifndef _WM_WINDOW_H
#define _WM_WINDOW_H

#include <stdint.h>
#include <stdbool.h>

/* 标题栏高度(像素) */
#define WM_TITLEBAR_H   20
/* 标题栏按钮(关闭/最小化)边长 */
#define WM_ICON_W       18
/* 窗口上限 */
#define WM_MAX_WINDOWS  16

typedef struct Window Window;

/* 可选：自定义窗口内容绘制回调。回调内用当前绘制色(vbeSetColor) +
 * VBE 绘图函数，以绝对屏幕坐标绘制窗口客户区内容。可为 NULL(画纯色客户区)。 */
typedef void (*WindowDrawFn)(Window* w);

typedef struct Window {
    int  x, y;            /* 屏幕左上角 */
    int  w, h;            /* 宽高 */
    const char* title;    /* 标题栏文本(ASCII/CJK) */
    uint32_t titleColor;  /* 标题栏颜色 */
    uint32_t bodyColor;   /* 客户区颜色 */
    bool visible;         /* 关闭后 false */
    bool minimized;       /* 最小化后 true */
    WindowDrawFn draw;    /* 自定义内容回调 */
} Window;

/* 初始化窗口管理器。bg 为已合成的背景帧缓冲(壁纸+任务栏)，尺寸 = width*height*4。
 * bg 由调用方长期持有，每次合成都会整帧拷贝为底。 */
int  wmInit(uint32_t* bg, int width, int height);

/* 创建窗口，返回窗口索引，满则返回 -1。 */
int  wmCreate(const char* title, int x, int y, int w, int h,
              uint32_t titleColor, uint32_t bodyColor, WindowDrawFn draw);

/* 全帧重合成：把 bg + 所有可见窗口(数组尾=最上层)画入 fb(RAM 缓冲)。 */
void wmComposite(uint32_t* fb);

/* 把 bg + 除 skipIndex 外的所有可见窗口合成进 fb。
 * 用于拖动期间构建"静止背景"(不含正在拖动的窗口)，避免每帧全量重渲染。 */
void wmCompositeExcluding(uint32_t* fb, int skipIndex);

/* 只把窗口 index 画入当前帧缓冲 fb(以绝对坐标覆盖其上)。
 * 与 wmCompositeExcluding 配合：拖动每帧在 back 上叠画被拖窗口。 */
void wmCompositeOnly(uint32_t* fb, int index);

/* 返回包含该点的最上层可见窗口索引，未命中返回 -1。 */
int  wmHitTest(int x, int y);

/* 命中区域常量(供 wmHitAction 返回) */
#define WM_ACT_CLIENT   0   /* 客户区：仅聚焦 */
#define WM_ACT_DRAG     1   /* 标题栏(非按钮)：聚焦+可拖拽 */
#define WM_ACT_MINIMIZE 2   /* 最小化按钮 */
#define WM_ACT_CLOSE    3   /* 关闭按钮 */

/* 判定点击窗口 index 内某点落在哪一区域，返回 WM_ACT_*。 */
int  wmHitAction(int index, int x, int y);

/* 将窗口置顶聚焦(移到数组尾部)，返回其新索引。 */
int  wmFocus(int index);

/* 读取窗口当前左上角坐标。 */
void wmGetPos(int index, int* x, int* y);

/* 读取窗口当前矩形(左上角+宽高)。用于脏矩形刷新。 */
void wmGetRect(int index, int* x, int* y, int* w, int* h);

/* 运行时自定义窗口客户区背景颜色(可任意改, 次帧合成即生效)。 */
void wmSetBodyColor(int index, uint32_t color);

/* 运行时自定义窗口标题栏颜色。 */
void wmSetTitleColor(int index, uint32_t color);

/* 移动窗口并 clamp 到屏幕内。 */
void wmMove(int index, int x, int y);

void wmClose(int index);
void wmMinimize(int index);
void wmRestore(int index);

int  wmWindowCount(void);

#endif