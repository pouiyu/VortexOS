// window.c
// 简单合成窗口管理器：持有一组窗口(数组序为 z 序，尾部最上层)，
// 全帧重合成到 RAM 帧缓冲(shadow)，由调用方再整体拷回 LFB。
// 所有窗口绘图走 VBE 单一当前绘制色模型：vbeSetColor 后绘制。
#include <wm/window.h>
#include <stdio/vbe.h>
#include <string/string.h>

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
             uint32_t titleColor, uint32_t bodyColor, WindowDrawFn draw) {
    if (gWmCount >= WM_MAX_WINDOWS) return -1;
    Window* win = &gWindows[gWmCount];
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->title = title;
    win->titleColor = titleColor;
    win->bodyColor = bodyColor;
    win->visible = true;
    win->minimized = false;
    win->draw = draw;
    return gWmCount++;
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

/* 标题栏按钮矩形：which=2 最小化，=3 关闭 */
static void winButtonRect(const Window* w, int which, int* bx, int* by, int* bw, int* bh) {
    *by = w->y + 1;
    *bw = WM_ICON_W;
    *bh = WM_TITLEBAR_H - 2;
    if (which == WM_ACT_CLOSE)
        *bx = w->x + w->w - WM_ICON_W - 2;
    else
        *bx = w->x + w->w - 2 * WM_ICON_W - 4;
}

/* 判定点击窗口 index 落在哪个区域 */
int wmHitAction(int index, int x, int y) {
    if (index < 0 || index >= gWmCount) return WM_ACT_DRAG;
    const Window* w = &gWindows[index];
    int bx, by, bw, bh;
    winButtonRect(w, WM_ACT_MINIMIZE, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) return WM_ACT_MINIMIZE;
    winButtonRect(w, WM_ACT_CLOSE, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) return WM_ACT_CLOSE;
    if (y < w->y + WM_TITLEBAR_H) return WM_ACT_DRAG;
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

/* 画单个窗口：边框 + 标题栏 + 客户区 + 自定义内容 + 右上角按钮 */
static void wmDrawWindow(const Window* w) {
    vbeSetColor(vbeColor(255, 255, 255));
    vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h);
    /* 边框(深灰) */
    vbeSetColor(vbeColor(70, 70, 70));
    vbeDrawLineRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h);
    /* 标题栏(加亮一点) */
    vbeSetColor(w->titleColor);
    vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, WM_TITLEBAR_H);
    /* 客户区 */
    vbeSetColor(w->bodyColor);
    vbeDrawFillRect((uint32_t)w->x, (uint32_t)w->y + WM_TITLEBAR_H, (uint32_t)w->w, (uint32_t)w->h - WM_TITLEBAR_H);
    /* 标题文本 */
    if (w->title) {
        vbeSetColor(vbeColor(255, 255, 255));
        vbeDrawStringCJK((uint16_t)(w->x + 6), (uint16_t)(w->y + 2), w->title);
    }
    /* 自定义内容 */
    if (w->draw) w->draw((Window*)w);

    /* 最小化按钮(_) */
    int bx, by, bw, bh;
    winButtonRect(w, WM_ACT_MINIMIZE, &bx, &by, &bw, &bh);
    vbeSetColor(vbeColor(120, 120, 120));
    vbeDrawFillRect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh);
    vbeSetColor(vbeColor(0, 0, 0));
    vbeDrawLine((uint32_t)(bx + 4), (uint32_t)(by + bh - 5), (uint32_t)(bx + bw - 5), (uint32_t)(by + bh - 5));
    /* 关闭按钮(x) */
    winButtonRect(w, WM_ACT_CLOSE, &bx, &by, &bw, &bh);
    vbeSetColor(vbeColor(200, 60, 60));
    vbeDrawFillRect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh);
    vbeSetColor(vbeColor(255, 255, 255));
    vbeDrawLine((uint32_t)(bx + 5), (uint32_t)(by + 5), (uint32_t)(bx + bw - 6), (uint32_t)(by + bh - 6));
    vbeDrawLine((uint32_t)(bx + bw - 6), (uint32_t)(by + 5), (uint32_t)(bx + 5), (uint32_t)(by + bh - 6));
}

void wmComposite(uint32_t* fb) {
    if (!fb || !gBg) return;

    memcpy(fb, gBg, (size_t)gWmW * (size_t)gWmH * 4);
    vbeBeginRamFrame(fb);
    for (int i = 0; i < gWmCount; i++) {
        const Window* w = &gWindows[i];
        if (w->visible && !w->minimized)
            wmDrawWindow(w);
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
        if (w->visible && !w->minimized)
            wmDrawWindow(w);
    }
    vbeEndRamFrame();
}

void wmCompositeOnly(uint32_t* fb, int index) {
    if (!fb || index < 0 || index >= gWmCount) return;
    vbeBeginRamFrame(fb);
    const Window* w = &gWindows[index];
    if (w->visible && !w->minimized)
        wmDrawWindow(w);
    vbeEndRamFrame();
}