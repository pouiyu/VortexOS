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

/* 窗口样式/能力标志：按位组合，创建时可指定，运行时可经 wmSetStyle/wmAddStyle/
 * wmRemoveStyle 动态增删。绘制与命中测试都按当前 style 生效。
 * 程序可借此自定义窗口外观与行为，例如移除最小化/关闭按钮。 */
#define WM_STYLE_BORDER    0x0001   /* 边框 */
#define WM_STYLE_TITLE     0x0002   /* 标题栏(含标题文本) */
#define WM_STYLE_MINIMIZE  0x0004   /* 最小化按钮(需 WM_STYLE_TITLE) */
#define WM_STYLE_CLOSE     0x0008   /* 关闭按钮(需 WM_STYLE_TITLE) */
#define WM_STYLE_DRAG      0x0010   /* 标题栏可拖拽移动 */

/* 常规窗口默认样式 */
#define WM_STYLE_DEFAULT (WM_STYLE_BORDER | WM_STYLE_TITLE | WM_STYLE_MINIMIZE \
                        | WM_STYLE_CLOSE | WM_STYLE_DRAG)

typedef struct Window {
    int  x, y;            /* 屏幕左上角 */
    int  w, h;            /* 宽高 */
    const char* title;    /* 标题栏文本(ASCII/CJK) */
    uint32_t titleColor;  /* 标题栏颜色 */
    uint32_t bodyColor;   /* 客户区底色(首次分配 surface 时的初始填充) */
    uint32_t style;       /* WM_STYLE_* 位组合 */
    bool visible;         /* 关闭后 false */
    bool minimized;       /* 最小化后 true */
    WindowDrawFn draw;    /* 自定义内容回调(内核窗口用；与 surface 二选一) */

    /* 客户区 surface：用户程序经 syscall 绘制的 RAM 像素缓冲，
     * 宽=w、高=h-WM_TITLEBAR_H、行宽 stride=w，首次绘制时懒分配并填 bodyColor。
     * 合成时若存在则整块贴回客户区；NULL 时客户区直接用 bodyColor 纯色。 */
    uint32_t* surface;
    uint32_t  drawColor;  /* 客户区绘制当前色(单一绘制色模型，先 wmSetDrawColor 再画) */
} Window;

/* 初始化窗口管理器。bg 为已合成的背景帧缓冲(壁纸+任务栏)，尺寸 = width*height*4。
 * bg 由调用方长期持有，每次合成都会整帧拷贝为底。 */
int  wmInit(uint32_t* bg, int width, int height);

/* 创建窗口，返回窗口索引，满则返回 -1。
 * style 传 WM_STYLE_* 位组合；常规窗口用 WM_STYLE_DEFAULT。 */
int  wmCreate(const char* title, int x, int y, int w, int h,
              uint32_t titleColor, uint32_t bodyColor, uint32_t style,
              WindowDrawFn draw);

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

/* 运行时修改窗口标题文本(指向的字符串须长期有效)。 */
void wmSetTitle(int index, const char* title);

/* 运行时更换自定义内容绘制回调(NULL 画纯色客户区)。 */
void wmSetDraw(int index, WindowDrawFn draw);

/* 窗口样式/能力查询与修改：返回当前 WM_STYLE_* 组合。 */
uint32_t wmGetStyle(int index);
void     wmSetStyle(int index, uint32_t style);   /* 整体替换 */
void     wmAddStyle(int index, uint32_t flags);   /* 叠加能力 */
void     wmRemoveStyle(int index, uint32_t flags);/* 移除能力 */

/* 移动窗口并 clamp 到屏幕内。 */
void wmMove(int index, int x, int y);

void wmClose(int index);
void wmMinimize(int index);
void wmRestore(int index);

int  wmWindowCount(void);

/* ---- 窗口客户区 surface 绘制命令(供 syscall 服务/内核调用) ----
 * 绘制到窗口客户区的 RAM surface：坐标均为客户区相对坐标(0,0=客户区左上角)，
 * 超出客户区自动裁剪。使用 vbe 单一当前绘制色模型：先 wmSetDrawColor(index,color)
 * 再调用绘制命令。合成时窗口 surface 整块贴回屏幕对应位置。 */
void wmSetDrawColor(int index, uint32_t color);             /* 设置客户区当前绘制色 */
void wmFillRect(int index, int cx, int cy, int cw, int ch); /* 用 drawColor 填充矩形 */
void wmDrawText(int index, int cx, int cy, const char* text);/* 用 drawColor 画文本(CJK) */
void wmDrawLine(int index, int x0, int y0, int x1, int y1);  /* 用 drawColor 画直线 */
void wmDrawLineRect(int index, int cx, int cy, int cw, int ch); /* 用 drawColor 画线框矩形 */

#endif