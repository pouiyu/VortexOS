// window.c
// 简单合成窗口管理器：持有一组窗口(数组序为 z 序，尾部最上层)，
// 全帧重合成到 RAM 帧缓冲(shadow)，由调用方再整体拷回 LFB。
// 所有窗口绘图走 VBE 单一当前绘制色模型：vbeSetColor 后绘制。
#include <wm/window.h>
#include <stdio/vbe.h>
#include <string/string.h>
#include <stdlib/stdlib.h>
#include <serial.h>

/* 命中结果：点击窗口哪些区域 */
#define WM_ACT_CLIENT   0   /* 客户区：仅聚焦 */
#define WM_ACT_DRAG     1   /* 标题栏(非按钮)：聚焦+开始拖拽 */
#define WM_ACT_MINIMIZE 2   /* 最小化按钮 */
#define WM_ACT_CLOSE    3   /* 关闭按钮 */

static Window  gWindows[WM_MAX_WINDOWS];
static int     gWmCount = 0;
static uint32_t* gBg = NULL;
static int     gWmW = 0;
static int     gWmH = 0;

int wmInit(uint32_t* bg, int width, int height) {
    if (!bg) return -1;
    gBg = bg;
    gWmW = width;
    gWmH = height;
    gWmCount = 0;
    return 0;
}

int wmCreate(const char* title, int x, int y, int w, int h,
             uint32_t titleColor, uint32_t bodyColor, uint32_t style,
             WindowDrawFn draw) {
    if (gWmCount >= WM_MAX_WINDOWS) return -1;
    Window* win = &gWindows[gWmCount];
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->title = title;
    win->titleColor = titleColor;
    win->bodyColor = bodyColor;
    win->style = style;
    win->visible = true;
    win->minimized = false;
    win->draw = draw;
    win->surface = NULL;
    win->drawColor = bodyColor;
    return gWmCount++;
}

/* 懒分配窗口客户区 surface(宽=w、高=h-标题栏、行宽 stride=w)，首次填 bodyColor。
 * 失败返回 NULL。窗口尺寸后续不变(无 resize API)，分配一次即长期复用。 */
static uint32_t* winSurface(Window* w) {
    if (w->surface) return w->surface;
    int cw = w->w;
    int ch = w->h - WM_TITLEBAR_H;
    if (cw <= 0 || ch <= 0) return NULL;
    uint32_t* s = malloc((size_t)cw * (size_t)ch * 4);
    if (!s) return NULL;
    for (int i = 0; i < cw * ch; i++) s[i] = w->bodyColor;
    w->surface = s;
    return s;
}

/* 窗口矩形是否含点 */
static bool winContains(const Window* w, int x, int y) {
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
}

int wmHitTest(int x, int y) {
    for (int i = gWmCount - 1; i >= 0; i--) {
        const Window* w = &gWindows[i];
        if (w->visible && !w->minimized && winContains(w, x, y))
            return i;
    }
    return -1;
}

/* 标题栏按钮矩形：which=2 最小化，=3 关闭。
 * 按钮从右往左排列：关闭贴右缘；最小化在其左侧(若关闭按钮也被移除,
 * 则最小化贴右缘)。仅计算几何位置，是否可点由 wmHitAction 按 style 判定。 */
static void winButtonRect(const Window* w, int which, int* bx, int* by, int* bw, int* bh) {
    *by = w->y + 1;
    *bw = WM_ICON_W;
    *bh = WM_TITLEBAR_H - 2;
    bool hasClose = (w->style & WM_STYLE_CLOSE) != 0;
    if (which == WM_ACT_CLOSE)
        *bx = w->x + w->w - WM_ICON_W - 2;
    else /* WM_ACT_MINIMIZE */
        *bx = hasClose ? (w->x + w->w - 2 * WM_ICON_W - 4)
                       : (w->x + w->w - WM_ICON_W - 2);
}

/* 判定点击窗口 index 落在哪个区域 */
int wmHitAction(int index, int x, int y) {
    if (index < 0 || index >= gWmCount) return WM_ACT_DRAG;
    const Window* w = &gWindows[index];
    if (y < w->y + WM_TITLEBAR_H) {
        /* 标题栏区域：先按按钮判定，再按能力判定拖拽/仅聚焦 */
        if (w->style & WM_STYLE_MINIMIZE) {
            int bx, by, bw, bh;
            winButtonRect(w, WM_ACT_MINIMIZE, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) return WM_ACT_MINIMIZE;
        }
        if (w->style & WM_STYLE_CLOSE) {
            int bx, by, bw, bh;
            winButtonRect(w, WM_ACT_CLOSE, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) return WM_ACT_CLOSE;
        }
        if (w->style & WM_STYLE_DRAG) return WM_ACT_DRAG;
        return WM_ACT_CLIENT;   /* 有标题栏但不可拖：点击仅聚焦 */
    }
    return WM_ACT_CLIENT;
}

int wmFocus(int index) {
    if (index < 0 || index >= gWmCount) return index;
    Window tmp = gWindows[index];
    for (int i = index; i < gWmCount - 1; i++)
        gWindows[i] = gWindows[i + 1];
    gWindows[gWmCount - 1] = tmp;
    return gWmCount - 1;
}

void wmGetPos(int index, int* x, int* y) {
    if (index < 0 || index >= gWmCount) return;
    *x = gWindows[index].x;
    *y = gWindows[index].y;
}

void wmGetRect(int index, int* x, int* y, int* w, int* h) {
    if (index < 0 || index >= gWmCount) return;
    *x = gWindows[index].x;
    *y = gWindows[index].y;
    *w = gWindows[index].w;
    *h = gWindows[index].h;
}

void wmSetBodyColor(int index, uint32_t color) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].bodyColor = color;
}

void wmSetTitleColor(int index, uint32_t color) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].titleColor = color;
}

void wmSetTitle(int index, const char* title) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].title = title;
}

void wmSetDraw(int index, WindowDrawFn draw) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].draw = draw;
}

uint32_t wmGetStyle(int index) {
    if (index < 0 || index >= gWmCount) return 0;
    return gWindows[index].style;
}

void wmSetStyle(int index, uint32_t style) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].style = style;
}

void wmAddStyle(int index, uint32_t flags) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].style |= flags;
}

void wmRemoveStyle(int index, uint32_t flags) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].style &= ~flags;
}

void wmMove(int index, int x, int y) {
    if (index < 0 || index >= gWmCount) return;
    Window* w = &gWindows[index];
    if (x < 0) x = 0;
    if (x > gWmW - w->w) x = gWmW - w->w;
    if (y < 0) y = 0;
    if (y > gWmH - w->h) y = gWmH - w->h;
    if (w->w > gWmW) x = 0;
    if (w->h > gWmH) y = 0;
    w->x = x;
    w->y = y;
}

void wmClose(int index) {
    if (index >= 0 && index < gWmCount) gWindows[index].visible = false;
}

void wmMinimize(int index) {
    if (index >= 0 && index < gWmCount) gWindows[index].minimized = true;
}

void wmRestore(int index) {
    if (index >= 0 && index < gWmCount) gWindows[index].minimized = false;
}

int wmWindowCount(void) {
    return gWmCount;
}

/* ==================== 客户区 surface 绘制命令 ==================== */

void wmSetDrawColor(int index, uint32_t color) {
    if (index < 0 || index >= gWmCount) return;
    gWindows[index].drawColor = color;
}

/* 进入窗口 index 的客户区 surface 渲染上下文：此后 vbe 绘图函数
 * 以屏幕坐标写入客户区 surface(内部换算+裁剪)。需配对 vbeEndRamWindow。 */
static bool wmBeginClient(Window* w) {
    uint32_t* surf = winSurface(w);
    if (!surf) return false;
    vbeBeginRamWindow(surf, w->w, w->x, w->y + WM_TITLEBAR_H, w->w, w->h - WM_TITLEBAR_H);
    vbeSetColor(w->drawColor);
    return true;
}

void wmFillRect(int index, int cx, int cy, int cw, int ch) {
    if (index < 0 || index >= gWmCount) return;
    Window* w = &gWindows[index];
    if (!wmBeginClient(w)) return;
    vbeDrawFillRect((uint32_t)(w->x + cx), (uint32_t)(w->y + WM_TITLEBAR_H + cy),
                    (uint32_t)cw, (uint32_t)ch);
    vbeEndRamWindow();
}

void wmDrawText(int index, int cx, int cy, const char* text) {
    if (index < 0 || index >= gWmCount || !text) return;
    Window* w = &gWindows[index];
    if (!wmBeginClient(w)) return;
    vbeDrawStringCJK((uint16_t)(w->x + cx), (uint16_t)(w->y + WM_TITLEBAR_H + cy), text);
    vbeEndRamWindow();
}

void wmDrawLine(int index, int x0, int y0, int x1, int y1) {
    if (index < 0 || index >= gWmCount) return;
    Window* w = &gWindows[index];
    if (!wmBeginClient(w)) return;
    vbeDrawLine((uint32_t)(w->x + x0), (uint32_t)(w->y + WM_TITLEBAR_H + y0),
                (uint32_t)(w->x + x1), (uint32_t)(w->y + WM_TITLEBAR_H + y1));
    vbeEndRamWindow();
}

void wmDrawLineRect(int index, int cx, int cy, int cw, int ch) {
    if (index < 0 || index >= gWmCount) return;
    Window* w = &gWindows[index];
    if (!wmBeginClient(w)) return;
    vbeDrawLineRect((uint32_t)(w->x + cx), (uint32_t)(w->y + WM_TITLEBAR_H + cy),
                    (uint32_t)cw, (uint32_t)ch);
    vbeEndRamWindow();
}

/* 把窗口 surface 整块贴到帧缓冲 fb 的客户区位置(带屏幕裁剪)。
 * 仅当窗口有 surface 且无内核回调时使用；回调窗口的客户区由回调直接绘制。 */
static void wmBlitSurface(const Window* w, uint32_t* fb) {
    if (!w->surface) return;
    int cw = w->w;
    int ch = w->h - WM_TITLEBAR_H;
    if (cw <= 0 || ch <= 0) return;

    for (int row = 0; row < ch; row++) {
        int yy = w->y + WM_TITLEBAR_H + row;
        if (yy < 0 || yy >= gWmH) continue;
        int x0 = w->x;
        int x1 = w->x + cw;
        if (x0 < 0) x0 = 0;
        if (x1 > gWmW) x1 = gWmW;
        if (x0 >= x1) continue;
        memcpy(&fb[(size_t)yy * gWmW + x0],
               &w->surface[(size_t)row * cw + (x0 - w->x)],
               (size_t)(x1 - x0) * 4);
    }
}

/* 画单个窗口：按 style 决定边框/标题栏/按钮/内容是否绘制 */
static void wmDrawWindow(const Window* w) {
    vbeSetColor(vbeColor(255, 255, 255));
    vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h);
    /* 边框(深灰)，仅当开启边框样式 */
    if (w->style & WM_STYLE_BORDER) {
        vbeSetColor(vbeColor(70, 70, 70));
        vbeDrawLineRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h);
    }
    /* 标题栏(加亮一点)，仅当开启标题栏样式 */
    if (w->style & WM_STYLE_TITLE) {
        vbeSetColor(w->titleColor);
        vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, WM_TITLEBAR_H);
    }
    /* 客户区 */
    vbeSetColor(w->bodyColor);
    vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y + WM_TITLEBAR_H, (uint32_t)w->w, (uint32_t)w->h - WM_TITLEBAR_H);
    /* 标题文本 */
    if ((w->style & WM_STYLE_TITLE) && w->title) {
        vbeSetColor(vbeColor(255, 255, 255));
        vbeDrawStringCJK((uint16_t)(w->x + 6), (uint16_t)(w->y + 2), w->title);
    }
    /* 自定义内容 */
    if (w->draw) w->draw((Window*)w);

    /* 标题栏按钮(仅标题栏样式下显示) */
    if (w->style & WM_STYLE_TITLE) {
        int bx, by, bw, bh;
        /* 最小化按钮(_) */
        if (w->style & WM_STYLE_MINIMIZE) {
            winButtonRect(w, WM_ACT_MINIMIZE, &bx, &by, &bw, &bh);
            vbeSetColor(vbeColor(120, 120, 120));
            vbeDrawFillRect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh);
            vbeSetColor(vbeColor(0, 0, 0));
            vbeDrawLine((uint32_t)(bx + 4), (uint32_t)(by + bh - 5), (uint32_t)(bx + bw - 5), (uint32_t)(by + bh - 5));
        }
        /* 关闭按钮(x) */
        if (w->style & WM_STYLE_CLOSE) {
            winButtonRect(w, WM_ACT_CLOSE, &bx, &by, &bw, &bh);
            vbeSetColor(vbeColor(200, 60, 60));
            vbeDrawFillRect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh);
            vbeSetColor(vbeColor(255, 255, 255));
            vbeDrawLine((uint32_t)(bx + 5), (uint32_t)(by + 5), (uint32_t)(bx + bw - 6), (uint32_t)(by + bh - 6));
            vbeDrawLine((uint32_t)(bx + bw - 6), (uint32_t)(by + 5), (uint32_t)(bx + 5), (uint32_t)(by + bh - 6));
        }
    }
}

void wmComposite(uint32_t* fb) {
    if (!fb || !gBg) return;

    memcpy(fb, gBg, (size_t)gWmW * (size_t)gWmH * 4);
    vbeBeginRamFrame(fb);
    for (int i = 0; i < gWmCount; i++) {
        const Window* w = &gWindows[i];
        if (w->visible && !w->minimized) {
            wmDrawWindow(w);
            if (w->surface && !w->draw) wmBlitSurface(w, fb);
        }
    }
    vbeEndRamFrame();
}

void wmCompositeExcluding(uint32_t* fb, int skipIndex) {
    if (!fb || !gBg) return;

    memcpy(fb, gBg, (size_t)gWmW * (size_t)gWmH * 4);
    vbeBeginRamFrame(fb);
    for (int i = 0; i < gWmCount; i++) {
        if (i == skipIndex) continue;
        const Window* w = &gWindows[i];
        if (w->visible && !w->minimized) {
            wmDrawWindow(w);
            if (w->surface && !w->draw) wmBlitSurface(w, fb);
        }
    }
    vbeEndRamFrame();
}

void wmCompositeOnly(uint32_t* fb, int index) {
    if (!fb || index < 0 || index >= gWmCount) return;
    vbeBeginRamFrame(fb);
    const Window* w = &gWindows[index];
    if (w->visible && !w->minimized) {
        wmDrawWindow(w);
        if (w->surface && !w->draw) wmBlitSurface(w, fb);
    }
    vbeEndRamFrame();
}