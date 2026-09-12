// libwidget.c —— 轻量 UI 组件库，构建在 libgui 之上，无堆分配。
// 供"可视化设计器生成代码"使用：Widget 数组描述一组控件，统一绘制/命中/交互。
#include <gui/libwidget.h>

/* 无 libc：自带迷你字符串助手 */
static int wdStrLen(const char* s) { int n = 0; while (s && s[n]) n++; return n; }
static void wdTxtCpy(char* dst, const char* src, int cap) {
    if (cap <= 0) return;
    int i = 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void wdInit(Widget* ws, int n) {
    for (int i = 0; i < n; i++) {
        if (ws[i].type == WD_TEXTBOX) {
            ws[i].len = 0;
            if (ws[i].buf && ws[i].cap > 0) {
                wdTxtCpy(ws[i].buf, ws[i].label, ws[i].cap);
                ws[i].len = wdStrLen(ws[i].buf);
            }
        }
        /* 其余运行态已有初始化好的默认值 */
    }
}

/* 文本像素宽估算：ASCII 8px / 其他(CJK) 16px */
static int wdTextWidth(const char* s) {
    int w = 0;
    while (s && *s) {
        unsigned char c = (unsigned char)*s++;
        w += (c < 0x80) ? 8 : 16;
    }
    return w;
}

/* 追踪已画过的文本行/列指针，返回 (x,y) 布局用；这里直接按需绘制 */
static void wdDrawOne(int win, const Widget* w) {
    switch (w->type) {
    case WD_BUTTON: {
        uint32_t face = w->down ? w->fg : w->bg;
        guiSetColor(win, face);
        guiFillRect(win, w->x, w->y, w->w, w->h);
        guiSetColor(win, w->border);
        guiDrawLineRect(win, w->x, w->y, w->w, w->h);
        /* 白字水平居中(估算宽度), 垂直居中 */
        int tw = wdTextWidth(w->label);
        int tx = w->x + (w->w - tw) / 2;
        if (tx < w->x) tx = w->x;
        int ty = w->y + (w->h - 16) / 2;
        if (ty < w->y) ty = w->y;
        guiSetColor(win, guiRgb(255, 255, 255));
        guiDrawText(win, tx, ty, w->label);
        break;
    }
    case WD_LABEL:
        guiSetColor(win, w->fg);
        guiDrawText(win, w->x, w->y, w->label);
        break;
    case WD_TEXTBOX: {
        /* 白底 + 边框 */
        guiSetColor(win, w->bg);
        guiFillRect(win, w->x, w->y, w->w, w->h);
        guiSetColor(win, w->border);
        guiDrawLineRect(win, w->x, w->y, w->w, w->h);
        /* 文本 + 光标位 */
        guiSetColor(win, w->fg);
        guiDrawText(win, w->x + 2, w->y + (w->h - 16) / 2, w->buf ? w->buf : "");
        break;
    }
    case WD_CHECKBOX: {
        int bx = w->x, by = w->y + (w->h - 14) / 2;
        if (by < w->y) by = w->y;
        guiSetColor(win, guiRgb(255, 255, 255));
        guiFillRect(win, bx, by, 14, 14);
        guiSetColor(win, w->border);
        guiDrawLineRect(win, bx, by, 14, 14);
        if (w->checked) {
            /* 打勾: 两段斜线 */
            guiSetColor(win, w->fg);
            guiDrawLine(win, bx + 2, by + 8, bx + 5, by + 11);
            guiDrawLine(win, bx + 5, by + 11, bx + 12, by + 3);
        }
        guiSetColor(win, w->fg);
        guiDrawText(win, bx + 20, w->y + (w->h - 16) / 2, w->label);
        break;
    }
    case WD_LINE:
        guiSetColor(win, w->border);
        if (w->w >= w->h)  /* 水平分隔线 */
            guiDrawLine(win, w->x, w->y + w->h / 2, w->x + w->w, w->y + w->h / 2);
        else               /* 垂直 */
            guiDrawLine(win, w->x + w->w / 2, w->y, w->x + w->w / 2, w->y + w->h);
        break;
    case WD_RECT:
        guiSetColor(win, w->border);
        guiDrawLineRect(win, w->x, w->y, w->w, w->h);
        break;
    }
}

void wdDraw(int win, Widget* ws, int n) {
    for (int i = 0; i < n; i++) wdDrawOne(win, &ws[i]);
}

int wdHit(const Widget* ws, int n, int mx, int my) {
    /* 后画的(数组靠后)在上层，命中优先 */
    for (int i = n - 1; i >= 0; i--) {
        const Widget* w = &ws[i];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return i;
    }
    return -1;
}

int wdPress(int win, Widget* ws, int n, int idx, int down) {
    if (idx < 0 || idx >= n) return 0;
    Widget* w = &ws[idx];
    int changed = 0;
    if (w->type == WD_BUTTON) {
        if (w->down != down) { w->down = down; changed = 1; }
    } else if (w->type == WD_CHECKBOX) {
        /* 仅在"按下"沿(从非按下到按下)切换，避免按住反复闪 */
        if (down && !w->down) { w->checked = !w->checked; changed = 1; }
        w->down = down;
    }
    if (changed) wdDrawOne(win, w);
    return changed;
}

int wdHandleKey(int win, Widget* ws, int n, int* focus, char key) {
    if (!focus || *focus < 0 || *focus >= n) return 0;
    Widget* w = &ws[*focus];
    if (w->type != WD_TEXTBOX || !w->buf || w->cap <= 0) return 0;

    int changed = 0;
    if (key >= 32 && key < 127) {
        /* 可打印 ASCII：append(留 NUL) */
        if (w->len < w->cap - 1) { w->buf[w->len++] = key; w->buf[w->len] = '\0'; changed = 1; }
    } else if (key == KEY_BACKSPACE || key == '\b' || key == 0x7F) {
        if (w->len > 0) { w->len--; w->buf[w->len] = '\0'; changed = 1; }
    } else if (key == KEY_ENTER) {
        /* 暂不换行，忽略 */
    }
    if (changed) wdDrawOne(win, w);
    return changed;
}