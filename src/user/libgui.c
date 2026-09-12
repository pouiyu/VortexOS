// libgui.c —— 用户态 GUI 库实现
// 每个函数只做"参数打包 + int 0x80"，不依赖任何 libc/内核符号，
// 与 gui_demo.c 一起独立链接成 ELF 用户程序。
#include <gui/libgui.h>

/* 3 参系统调用：EAX=num, EBX=a, ECX=b, EDX=c，返回值在 EAX。
 * memory clobber：内核会经身份映射写本程序的 WmCreateArgs/WmEvent 等。 */
static int syscall3(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(num), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return ret;
}

int guiCreateWindow(const char* title, int x, int y, int w, int h,
                    uint32_t titleColor, uint32_t bodyColor, uint32_t style) {
    WmCreateArgs args;
    args.title      = title;
    args.x          = x;
    args.y          = y;
    args.w          = w;
    args.h          = h;
    args.titleColor = titleColor;
    args.bodyColor  = bodyColor;
    args.style      = style;
    return syscall3(SYS_WM_CREATE, (uint32_t)(uintptr_t)&args, 0, 0);
}

void guiSetColor(int win, uint32_t color) {
    syscall3(SYS_WM_SET_COLOR, (uint32_t)win, color, 0);
}

void guiFillRect(int win, int cx, int cy, int cw, int ch) {
    syscall3(SYS_WM_FILL_RECT, (uint32_t)win,
             (uint32_t)((uint16_t)cx | ((uint32_t)(uint16_t)cy << 16)),
             (uint32_t)((uint16_t)cw | ((uint32_t)(uint16_t)ch << 16)));
}

void guiDrawText(int win, int cx, int cy, const char* text) {
    syscall3(SYS_WM_DRAW_TEXT, (uint32_t)win,
             (uint32_t)((uint16_t)cx | ((uint32_t)(uint16_t)cy << 16)),
             (uint32_t)(uintptr_t)text);
}

void guiDrawLine(int win, int x0, int y0, int x1, int y1) {
    syscall3(SYS_WM_DRAW_LINE, (uint32_t)win,
             (uint32_t)((uint16_t)x0 | ((uint32_t)(uint16_t)y0 << 16)),
             (uint32_t)((uint16_t)x1 | ((uint32_t)(uint16_t)y1 << 16)));
}

void guiDrawLineRect(int win, int cx, int cy, int cw, int ch) {
    syscall3(SYS_WM_DRAW_LINE_RECT, (uint32_t)win,
             (uint32_t)((uint16_t)cx | ((uint32_t)(uint16_t)cy << 16)),
             (uint32_t)((uint16_t)cw | ((uint32_t)(uint16_t)ch << 16)));
}

void guiSetTitle(int win, const char* title) {
    syscall3(SYS_WM_SET_TITLE, (uint32_t)win, (uint32_t)(uintptr_t)title, 0);
}

void guiGetWinPos(int win, int* x, int* y) {
    WmPos pos;
    syscall3(SYS_WM_GET_POS, (uint32_t)win, (uint32_t)(uintptr_t)&pos, 0);
    if (x) *x = pos.x;
    if (y) *y = pos.y;
}

void guiClose(int win) {
    syscall3(SYS_WM_CLOSE, (uint32_t)win, 0, 0);
}

void guiFlush(void) {
    syscall3(SYS_WM_FLUSH, 0, 0, 0);
}

void guiPoll(WmEvent* ev) {
    syscall3(SYS_WM_POLL, (uint32_t)(uintptr_t)ev, 0, 0);
}

void guiExit(void) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT) : "memory");
    for (;;) __asm__ volatile ("hlt");   /* 不会到达 */
}
