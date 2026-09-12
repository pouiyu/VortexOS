// wmsvc.c —— 内核 WM 服务
// 为"用户态 GUI 程序经 syscall 创建窗口/绘制 UI"提供服务。
// 窗口的创建/绘制命令直接转给 wm/window.c；输入事件循环(拖拽/关闭/最小化/
// 聚焦)、场景合成、LFB 回写、鼠标指针绘制全部收敛在此，替代旧的
// graphic_main 内联事件循环。
#include <wm/wmsvc.h>
#include <wm/window.h>
#include <stdio/vbe.h>
#include <mouse.h>
#include <keyboard.h>
#include <serial.h>
#include <string/string.h>

#define POINTER_W 10
#define POINTER_H 10

static uint32_t* sShadow  = NULL;
static uint32_t* sDragBack = NULL;
static size_t    sBgPix   = 0;

/* 鼠标箭头位图(可选) */
static const uint8_t* sArrowPix = NULL;
static uint32_t sArrowW = 0, sArrowH = 0;

/* 拖拽/指针状态 */
static int sDragWm = -1;
static int sOffX = 0, sOffY = 0;
static uint8_t sPrevButtons = 0;
static int sNx = 0, sNy = 0;
static int sLastX = 0, sLastY = 0;
static int sPw = POINTER_W, sPh = POINTER_H;
static int sDragPrevX = 0, sDragPrevY = 0, sDragPrevW = 0, sDragPrevH = 0;

void wmsvcInit(uint32_t* shadow, uint32_t* dragBack, size_t bgPix) {
    sShadow = shadow;
    sDragBack = dragBack;
    sBgPix = bgPix;
    sDragWm = -1;
    sPrevButtons = 0;
    sNx = mouseGetX();
    sNy = mouseGetY();
    sLastX = sNx;
    sLastY = sNy;
    sPw = sArrowPix ? (int)sArrowW : POINTER_W;
    sPh = sArrowPix ? (int)sArrowH : POINTER_H;
}

void wmsvcSetArrow(const uint8_t* pixels, uint32_t w, uint32_t h) {
    sArrowPix = pixels;
    sArrowW = w;
    sArrowH = h;
    sPw = pixels ? (int)w : POINTER_W;
    sPh = pixels ? (int)h : POINTER_H;
}

/* 绘制鼠标指针: 有箭头位图用之, 否则回退"白方块+黑边" */
static void wmsvcDrawPointer(int x, int y) {
    if (sArrowPix) {
        if (x < vbeWidth && y < vbeHeight)
            vbeDrawBitmap((uint32_t)x, (uint32_t)y, sArrowPix, sArrowW, sArrowH);
        return;
    }
    for (int iy = 0; iy < POINTER_H; iy++) {
        for (int ix = 0; ix < POINTER_W; ix++) {
            bool border = (ix == 0 || ix == POINTER_W - 1 ||
                           iy == 0 || iy == POINTER_H - 1);
            vbeSetColor(border ? vbeColor(0, 0, 0) : vbeColor(255, 255, 255));
            int px = x + ix, py = y + iy;
            if (px >= 0 && px < vbeWidth && py >= 0 && py < vbeHeight)
                vbeDrawPixel((uint32_t)px, (uint32_t)py);
        }
    }
}

/* 整帧把场景快照写回 LFB */
static void wmsvcBlitFull(const uint32_t* scene, size_t pix) {
    memcpy((void*)(uintptr_t)gVbeInfo.lfbAddr, scene, pix * 4);
}

/* 把场景快照 scene(RAM, 无指针)中 rect 区域拷贝回 LFB(抹旧指针用) */
static void wmsvcBlitRect(const uint32_t* scene, int x, int y, int w, int h) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > vbeWidth)  x1 = vbeWidth;
    if (y1 > vbeHeight) y1 = vbeHeight;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t* lfb = (uint32_t*)(uintptr_t)gVbeInfo.lfbAddr;
    int bw = x1 - x0, bh = y1 - y0;
    for (int yy = 0; yy < bh; yy++) {
        const uint32_t* s = &scene[(y0 + yy) * vbeWidth + x0];
        uint32_t* d = &lfb[(y0 + yy) * vbeWidth + x0];
        for (int xx = 0; xx < bw; xx++) d[xx] = s[xx];
    }
}

/* 整帧重合成 + 写 LFB + 画指针 */
static void wmsvcRenderScene(void) {
    wmComposite(sShadow);
    wmsvcBlitFull(sShadow, sBgPix);
    wmsvcDrawPointer(sNx, sNy);
    sLastX = sNx;
    sLastY = sNy;
}

/* 拖动走"dragBack 还原旧位置 + 叠画被拖窗口 + 包围盒回写"的局部刷新路径 */
static void wmsvcRenderDrag(void) {
    int curX, curY, curW, curH;
    wmGetRect(sDragWm, &curX, &curY, &curW, &curH);
    /* 1) 用缓存还原旧位置背景 */
    for (int rp = sDragPrevY; rp < sDragPrevY + sDragPrevH; rp++) {
        if (rp < 0 || rp >= vbeHeight) continue;
        memcpy(&sShadow[(size_t)rp * vbeWidth + sDragPrevX],
               &sDragBack[(size_t)rp * vbeWidth + sDragPrevX],
               (size_t)sDragPrevW * 4);
    }
    /* 2) 把被拖窗口画到当前位置 */
    wmCompositeOnly(sShadow, sDragWm);
    /* 3) LFB 只回写新旧位置围成的包围盒 */
    int bMinX = sDragPrevX < curX ? sDragPrevX : curX;
    int bMinY = sDragPrevY < curY ? sDragPrevY : curY;
    int bMaxX = (sDragPrevX + sDragPrevW) > (curX + curW)
              ? (sDragPrevX + sDragPrevW) : (curX + curW);
    int bMaxY = (sDragPrevY + sDragPrevH) > (curY + curH)
              ? (sDragPrevY + sDragPrevH) : (curY + curH);
    wmsvcBlitRect(sShadow, bMinX, bMinY, bMaxX - bMinX, bMaxY - bMinY);
    wmsvcBlitRect(sShadow, sLastX, sLastY, sPw, sPh); /* 抹上一帧指针残留 */
    wmsvcDrawPointer(sNx, sNy);
    sDragPrevX = curX;
    sDragPrevY = curY;
    sDragPrevW = curW;
    sDragPrevH = curH;
    sLastX = sNx;
    sLastY = sNy;
}

/* SYS_WM_FLUSH: 用户程序改完客户区 surface 后上屏 */
static void wmsvcFlush(void) {
    wmsvcRenderScene();
}

/* 等待鼠标/键盘输入并处理 WM 交互，回填事件给用户程序。
 * 在 syscall 上下文中调用(中断已被 int 门关闭)，等待前需 sti。 */
static void wmsvcPoll(WmEvent* ev) {
    if (!ev) return;
    ev->key = 0;

    /* 等待输入唤醒(IRQ1/IRQ12) */
    __asm__ volatile ("sti");
    while (!mouseHasEvent() && !keyboardHasChar())
        __asm__ volatile ("hlt");

    bool sceneDirty = false;
    do {
        sNx = mouseGetX();
        sNy = mouseGetY();
        uint8_t btn = mouseGetButtons();

        int moveDx = 0, moveDy = 0;
        mouseGetMotion(&moveDx, &moveDy);
        (void)moveDx; (void)moveDy;

        /* 尽早取走一个待处理键(否则下方 while 的 keyboardHasChar() 恒为真, 死循环)。
         * 最多取一个：多个键只回填第一个, 其余留给下一次 SYS_WM_POLL。 */
        if (keyboardHasChar() && ev->key == 0)
            ev->key = keyboardGetChar();

        if (btn & MOUSE_LEFT_BUTTON) {
            if (!(sPrevButtons & MOUSE_LEFT_BUTTON)) {
                /* 按下瞬间: 命中测试并分发 */
                int idx = wmHitTest(sNx, sNy);
                if (idx != -1) {
                    int action = wmHitAction(idx, sNx, sNy);
                    if (action == WM_ACT_CLOSE) {
                        wmClose(idx);
                        sceneDirty = true;
                    } else if (action == WM_ACT_MINIMIZE) {
                        wmMinimize(idx);
                        sceneDirty = true;
                    } else if (action == WM_ACT_DRAG) {
                        idx = wmFocus(idx);
                        wmGetRect(idx, &sDragPrevX, &sDragPrevY,
                                  &sDragPrevW, &sDragPrevH);
                        sOffX = sNx - sDragPrevX;
                        sOffY = sNy - sDragPrevY;
                        wmsvcRenderScene();
                        wmCompositeExcluding(sDragBack, idx);
                        sDragWm = idx;
                    } else {
                        wmFocus(idx);
                        sceneDirty = true;
                    }
                }
            }
        } else if (sPrevButtons & MOUSE_LEFT_BUTTON) {
            /* 松开: 结束拖拽 */
            sDragWm = -1;
            sceneDirty = true;
        }
        sPrevButtons = btn;
        /* 已取到一个键就不再 spin；仅当仍有新鼠标包或还有未被取走的键时继续 */
    } while (mouseHasEvent() || (keyboardHasChar() && ev->key == 0));

    if (sDragWm != -1)
        wmMove(sDragWm, sNx - sOffX, sNy - sOffY);

    if (sDragWm != -1) {
        wmsvcRenderDrag();
    } else if (sceneDirty) {
        wmsvcRenderScene();
    } else if (sNx != sLastX || sNy != sLastY) {
        /* 仅指针移动 */
        wmsvcBlitRect(sShadow, sLastX, sLastY, sPw, sPh);
        wmsvcDrawPointer(sNx, sNy);
        sLastX = sNx;
        sLastY = sNy;
    }

    ev->mouseX = sNx;
    ev->mouseY = sNy;
    ev->buttons = sPrevButtons;
    ev->leftDown = (sPrevButtons & MOUSE_LEFT_BUTTON) ? 1 : 0;
    ev->flags = 0;
}

/* SYS_WM_* 分发入口 */
int wmsvcHandle(uint32_t fn, uint32_t a, uint32_t b, uint32_t c) {
    switch (fn) {
    case SYS_WM_CREATE: {
        WmCreateArgs* args = (WmCreateArgs*)a;
        if (!args) return -1;
        int idx = wmCreate(args->title, args->x, args->y, args->w, args->h,
                           args->titleColor, args->bodyColor, args->style, NULL);
        if (idx >= 0) serialPutStr("[WM] window created\n");
        return idx;
    }
    case SYS_WM_SET_COLOR:
        wmSetDrawColor((int)a, (uint32_t)b);
        return 0;
    case SYS_WM_FILL_RECT:
        wmFillRect((int)a, (int)(int16_t)(uint16_t)b, (int)(int16_t)(uint16_t)(b >> 16),
                   (int)(int16_t)(uint16_t)c, (int)(int16_t)(uint16_t)(c >> 16));
        return 0;
    case SYS_WM_DRAW_TEXT:
        wmDrawText((int)a, (int)(int16_t)(uint16_t)b, (int)(int16_t)(uint16_t)(b >> 16),
                   (const char*)c);
        return 0;
    case SYS_WM_DRAW_LINE:
        wmDrawLine((int)a, (int)(int16_t)(uint16_t)b, (int)(int16_t)(uint16_t)(b >> 16),
                   (int)(int16_t)(uint16_t)c, (int)(int16_t)(uint16_t)(c >> 16));
        return 0;
    case SYS_WM_DRAW_LINE_RECT:
        wmDrawLineRect((int)a, (int)(int16_t)(uint16_t)b, (int)(int16_t)(uint16_t)(b >> 16),
                       (int)(int16_t)(uint16_t)c, (int)(int16_t)(uint16_t)(c >> 16));
        return 0;
    case SYS_WM_SET_TITLE:
        wmSetTitle((int)a, (const char*)b);
        return 0;
    case SYS_WM_CLOSE:
        wmClose((int)a);
        return 0;
    case SYS_WM_GET_POS: {
        WmPos* pos = (WmPos*)b;
        if (!pos) return -1;
        wmGetPos((int)a, &pos->x, &pos->y);
        return 0;
    }
    case SYS_WM_FLUSH:
        wmsvcFlush();
        return 0;
    case SYS_WM_POLL:
        wmsvcPoll((WmEvent*)a);
        return 1;
    default:
        return -1;
    }
}
