#include "interruption/idt.h"
#include <keyboard.h>
#include <mouse.h>
#include <sound.h>
#include <stdio/vga.h>
#include <string/string.h>
#include <stdint.h>
#include <kernel.h>
#include <sysinfo/sysinfo.h>
#include <sysinfo/config.h>
#include <shell/shell.h>
#include <shell/commands.h>
#include <fs/fat32.h>
#include <fs/file.h>
#include <atapi.h>
#include "install.h"
#include "device.h"
#include <mm/pmm.h>
#include <mm/paging.h>
#include <stdlib/stdlib.h>
#include "tss.h"
#include "syscall.h"
#include "user.h"
#include "task.h"
#include <pit.h>
#include <usb/xhci.h>
#include <usb/hid.h>
#include <stdio/vbe.h>
#include <fs/bmp.h>
#include <wm/window.h>
#include <rtc.h>

uint8_t FG = COLOR_WHITE;
uint8_t BG = COLOR_BLACK;
uint8_t HL ;
uint8_t LL ;
uint8_t theme;

/* 图形模式 LFB 写屏统计(用于验证拖动条带刷新是否减少写屏) */
static uint32_t gWMWritePix = 0;
static uint32_t gWMWriteOps = 0;

// 光标样式(顶部/底部扫描线 0~15)，供菜单设置与 Shell 使用
uint8_t cursorTop = 14;
uint8_t cursorBottom = 15;

fat32Volume fsVolume;

#define MENU_STACK_SIZE 16

static const char* const ColorOptions[] = {
    "Black",
    "Blue",
    "Green",
    "Cyan",
    "Red",
    "Magenta",
    "Brown",
    "Light Gray",
    "Dark Gray",
    "Light Blue",
    "Light Green",
    "Light Cyan",
    "Light Red",
    "Light Magenta",
    "Yellow",
    "White"
};

unsigned int colorCount = (sizeof(ColorOptions) / sizeof(ColorOptions[0]));

typedef enum {
    MENU_MAIN,
    MENU_SETTINGS,
    MENU_SETTINGS_ABOUT,
    MENU_SETTINGS_THEME,
    MENU_SETTINGS_THEME_HL,   
    MENU_SETTINGS_THEME_LL, 
    MENU_SETTINGS_THEME_FG,
    MENU_SETTINGS_THEME_BG,
    MENU_SETTINGS_CURSOR,
    MENU_SETTINGS_CURSOR_TOP,
    MENU_SETTINGS_CURSOR_BOTTOM,
    MENU_CONFIRM_REBOOT,
    MENU_CONFIRM_SHUTDOWN
} MenuState;

typedef struct {
    MenuState menu;
    int selectedIndex;
} MenuFrame;

static MenuFrame menuStack[MENU_STACK_SIZE];
static int menuStackPtr = -1;

static MenuState currentMenu = MENU_MAIN;
static int selectedIndex = 0;
static int selectIndex = -1;

static const char* const mainOptions[] = {
    "Settings",
    "Shell",
    "Graphic",
    "Reboot",
    "Shutdown"
};

static const char* const settingsOptions[] = {
    "About",
    "Theme",
    "Cursor Style",
    "Back"
};

static const char* const aboutOptions[] = {
    "System Info",
    "Device Info",
    "Back"
};

static const char* const themeOptions[] = {
    "Highlight color",
    "Lowlight color",
    "Foreground color",
    "Background color",
    "Back"
};

static const char* const cursorOptions[] = {
    "Top scanline",
    "Bottom scanline",
    "Back"
};

static const char* const ScanlineOptions[] = {
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "10", "11", "12", "13", "14", "15"
};

unsigned int scanlineCount = (sizeof(ScanlineOptions) / sizeof(ScanlineOptions[0]));

static const char* const confirmOptions[] = {
    "Yes",
    "No"
};

#define MAIN_SIZE       (sizeof(mainOptions)     / sizeof(mainOptions[0]))
#define SETTINGS_SIZE   (sizeof(settingsOptions) / sizeof(settingsOptions[0]))
#define ABOUT_SIZE      (sizeof(aboutOptions)    / sizeof(aboutOptions[0]))
#define THEME_SIZE      (sizeof(themeOptions)    / sizeof(themeOptions[0]))
#define CURSOR_SIZE     (sizeof(cursorOptions)   / sizeof(cursorOptions[0]))
#define CONFIRM_SIZE    (sizeof(confirmOptions)  / sizeof(confirmOptions[0]))

static void pushMenu(MenuState menu, int index) {
    if (menuStackPtr < MENU_STACK_SIZE - 1) {
        menuStackPtr++;
        menuStack[menuStackPtr].menu = currentMenu;
        menuStack[menuStackPtr].selectedIndex = selectedIndex;
    }
    currentMenu = menu;
    selectedIndex = index;
    selectIndex = -1;
}

static bool popMenu(void) {
    if (menuStackPtr >= 0) {
        currentMenu = menuStack[menuStackPtr].menu;
        selectedIndex = menuStack[menuStackPtr].selectedIndex;
        selectIndex = -1;
        menuStackPtr--;
        return true;
    }
    return false;
}

void updateTheme(void) {
    theme = VGA_COLOR(FG, BG);
    vgaSetColorByte(theme);
}

static int getCurrentSize(void) {
    switch (currentMenu) {
        case MENU_MAIN:               return MAIN_SIZE;
        case MENU_SETTINGS:           return SETTINGS_SIZE;
        case MENU_SETTINGS_ABOUT:     return ABOUT_SIZE;
        case MENU_SETTINGS_THEME:     return THEME_SIZE;
        case MENU_SETTINGS_THEME_HL:
        case MENU_SETTINGS_THEME_LL:
        case MENU_SETTINGS_THEME_FG:
        case MENU_SETTINGS_THEME_BG:  return colorCount;
        case MENU_SETTINGS_CURSOR:    return CURSOR_SIZE;
        case MENU_SETTINGS_CURSOR_TOP:
        case MENU_SETTINGS_CURSOR_BOTTOM: return scanlineCount;
        case MENU_CONFIRM_REBOOT:
        case MENU_CONFIRM_SHUTDOWN:   return CONFIRM_SIZE;
        default:                      return 0;
    }
}

void vgaSetColorByte(uint8_t color) {
    vgaSetColor(color & 0x0F, (color >> 4) & 0x0F);
}

void drawTitle(const char* title) {
    vgaSetCursorPos(0, 0);
    vgaSetColorByte(theme);
    vgaPutChar(' ');
    vgaPutStr(title);
    vgaPutStr(" \n\n");
}

static void putOption(const char* text, bool hover) {
    uint8_t row, col;
    vgaGetCursorPos(&row, &col);

    vgaSetColorByte(hover ? vgaInvertColor(theme) : theme);

    for (int i = col; i < VGA_WIDTH; i++)
        vgaPutChar(' ');

    vgaSetCursorPos(row, col);
    vgaPutStr(text);
    vgaSetColorByte(theme);
}

static void drawOptions(const char* const* options, int size) {
    for (int i = 0; i < size; i++) {
        vgaSetCursorPos(2 + i, 0);
        putOption(options[i], selectedIndex == i);
    }
}

void drawMainMenu(void) {
    vgaDisableCursor();   // 菜单中始终隐藏光标(含从 Shell 返回的情况)
    vgaClear();
    drawTitle(OS_NAME "OS");
    drawOptions(mainOptions, MAIN_SIZE);
}

static void drawSettingsMenu(void) {
    vgaClear();
    drawTitle("Settings");
    drawOptions(settingsOptions, SETTINGS_SIZE);
}

static void drawAboutMenu(void) {
    vgaClear();
    drawTitle("About");
    drawOptions(aboutOptions, ABOUT_SIZE);
}

static void drawThemeMenu(const char* label) {
    vgaClear();
    drawTitle(label);
    drawOptions(ColorOptions, colorCount);
}

static void drawThemeOptionsMenu(void) {
    vgaClear();
    drawTitle("Theme");
    drawOptions(themeOptions, THEME_SIZE);
}

static void drawCursorMenu(void) {
    vgaClear();
    drawTitle("Cursor Style");
    drawOptions(cursorOptions, CURSOR_SIZE);
}

static void drawScanlineMenu(const char* label) {
    vgaClear();
    drawTitle(label);
    drawOptions(ScanlineOptions, scanlineCount);
}

static void drawConfirmMenu(const char* title) {
    vgaClear();
    drawTitle(title);
    drawOptions(confirmOptions, CONFIRM_SIZE);
}

static void drawCurrentMenu(void) {
    switch (currentMenu) {
        case MENU_MAIN:               drawMainMenu();              break;
        case MENU_SETTINGS:           drawSettingsMenu();          break;
        case MENU_SETTINGS_ABOUT:     drawAboutMenu();             break;
        case MENU_SETTINGS_THEME:     drawThemeOptionsMenu();      break;
        case MENU_SETTINGS_THEME_HL:  drawThemeMenu("Highlight Color"); break;
        case MENU_SETTINGS_THEME_LL:  drawThemeMenu("Lowlight Color");  break;
        case MENU_SETTINGS_THEME_FG:  drawThemeMenu("Foreground Color"); break;
        case MENU_SETTINGS_THEME_BG:  drawThemeMenu("Background Color"); break;
        case MENU_SETTINGS_CURSOR:        drawCursorMenu();               break;
        case MENU_SETTINGS_CURSOR_TOP:    drawScanlineMenu("Cursor Top Scanline");    break;
        case MENU_SETTINGS_CURSOR_BOTTOM: drawScanlineMenu("Cursor Bottom Scanline"); break;
        case MENU_CONFIRM_REBOOT:     drawConfirmMenu("Reboot?");  break;
        case MENU_CONFIRM_SHUTDOWN:   drawConfirmMenu("Shutdown?"); break;
    }
}

void messageBox(const char* msg) {
    vgaPutStr(msg);
    vgaPutStrColor("\n\nPress any key to return...", LL);
    while (!keyboardHasChar()) __asm__ volatile ("hlt");
    keyboardGetChar();
}

/* 图形模式入口（在菜单中选择 graphic 时调用），实现见文件底部 */
void graphic_main(unsigned int magic, unsigned int addr);

static void handleMainMenuSelect(void) {
    switch (selectIndex) {
        case 0: pushMenu(MENU_SETTINGS, 0);          break;
        case 1: runShell();                          break;  // Shell
        case 2: graphic_main(0, 0);                  break;  // 进入图形模式
        case 3: pushMenu(MENU_CONFIRM_REBOOT, 0);    break;
        case 4: pushMenu(MENU_CONFIRM_SHUTDOWN, 0);  break;
    }
    if (selectIndex != 1)  // Shell 自己刷新屏幕
        drawCurrentMenu();
}

static void handleSettingsMenuSelect(void) {
    switch (selectIndex) {
        case 0:  // About
            pushMenu(MENU_SETTINGS_ABOUT, 0);
            drawCurrentMenu();
            return;
        case 1:  // Theme
            pushMenu(MENU_SETTINGS_THEME, 0);
            drawCurrentMenu();
            return;
        case 2:  // Cursor Style
            pushMenu(MENU_SETTINGS_CURSOR, 0);
            drawCurrentMenu();
            return;
        case 3:  // Back
            if (!popMenu()) currentMenu = MENU_MAIN;
            drawCurrentMenu();
            return;
    }
    selectIndex = -1;
}

static void handleAboutSelect(void) {
    switch (selectIndex) {
        case 0:  // System Info
            showSystemInfo();
            break;
        case 1:  // Device Info
            showDeviceInfo();
            break;
        case 2:  // Back
            popMenu();
            break;
    }
    drawCurrentMenu();
}

static void handleThemeSelect(void) {
    switch (selectIndex) {
        case 0:
            pushMenu(MENU_SETTINGS_THEME_HL, HL);
            drawCurrentMenu();
            return;
        case 1:
            pushMenu(MENU_SETTINGS_THEME_LL, LL);
            drawCurrentMenu();
            return;
        case 2:
            pushMenu(MENU_SETTINGS_THEME_FG, FG);
            drawCurrentMenu();
            return;
        case 3:
            pushMenu(MENU_SETTINGS_THEME_BG, BG);
            drawCurrentMenu();
            return;
        case 4:
            popMenu();
            drawCurrentMenu();
            return;
    }
    selectIndex = -1;
}

static void handleThemeFGSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < (unsigned int)colorCount) {
        uint8_t newFG = (uint8_t)selectIndex;
        if (newFG == BG) {
            vgaClear();
            messageBox("Foreground cannot be the\nsame as background!");
        } else {
            FG = newFG;
            updateTheme();
        }
    }
    popMenu();
    drawCurrentMenu();
}

static void handleThemeHLSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < (unsigned int)colorCount) {
        HL = VGA_COLOR((uint8_t)selectIndex, BG);
    }
    popMenu();
    drawCurrentMenu();
}

static void handleThemeLLSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < (unsigned int)colorCount) {
        LL = VGA_COLOR((uint8_t)selectIndex, BG);
    }
    popMenu();
    drawCurrentMenu();
}

static void handleThemeBGSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < (unsigned int)colorCount) {
        uint8_t newBG = (uint8_t)selectIndex;
        if (newBG == FG) {
            vgaClear();
            messageBox("Background cannot be the\nsame as foreground!");
        } else {
            BG = newBG;
            HL = VGA_COLOR(HL & 0x0F, BG); 
            LL = VGA_COLOR(LL & 0x0F, BG);
            updateTheme();
        }
    }
    popMenu();
    drawCurrentMenu();
}

static void handleCursorSelect(void) {
    switch (selectIndex) {
        case 0:  // Top scanline
            pushMenu(MENU_SETTINGS_CURSOR_TOP, cursorTop);
            drawCurrentMenu();
            return;
        case 1:  // Bottom scanline
            pushMenu(MENU_SETTINGS_CURSOR_BOTTOM, cursorBottom);
            drawCurrentMenu();
            return;
        case 2:  // Back
            popMenu();
            drawCurrentMenu();
            return;
    }
    selectIndex = -1;
}

static void handleCursorTopSelect(void) {
    if (selectIndex >= 0 && selectIndex < (int)scanlineCount) {
        cursorTop = (uint8_t)selectIndex;   // 仅保存，Shell 进入时应用
    }
    popMenu();
    drawCurrentMenu();
}

static void handleCursorBottomSelect(void) {
    if (selectIndex >= 0 && selectIndex < (int)scanlineCount) {
        cursorBottom = (uint8_t)selectIndex;   // 仅保存，Shell 进入时应用
    }
    popMenu();
    drawCurrentMenu();
}

static void handleConfirmReboot(void) {
    if (selectIndex == 0) {
        deviceReboot();
    } else {
        popMenu();
        drawCurrentMenu();
    }
}

static void handleConfirmShutdown(void) {
    if (selectIndex == 0) {
        deviceShutdown();
    } else {
        popMenu();
        drawCurrentMenu();
    }
}

static void handleSelect(void) {
    if (selectIndex == -1) return;

    switch (currentMenu) {
        case MENU_MAIN:               handleMainMenuSelect();    break;
        case MENU_SETTINGS:           handleSettingsMenuSelect(); break;
        case MENU_SETTINGS_ABOUT:     handleAboutSelect();       break;
        case MENU_SETTINGS_THEME:     handleThemeSelect();       break;
        case MENU_SETTINGS_THEME_HL:  handleThemeHLSelect();   break;
        case MENU_SETTINGS_THEME_LL:  handleThemeLLSelect();   break;
        case MENU_SETTINGS_THEME_FG:  handleThemeFGSelect();     break;
        case MENU_SETTINGS_THEME_BG:  handleThemeBGSelect();     break;
        case MENU_SETTINGS_CURSOR:        handleCursorSelect();        break;
        case MENU_SETTINGS_CURSOR_TOP:    handleCursorTopSelect();     break;
        case MENU_SETTINGS_CURSOR_BOTTOM: handleCursorBottomSelect();  break;
        case MENU_CONFIRM_REBOOT:     handleConfirmReboot();     break;
        case MENU_CONFIRM_SHUTDOWN:   handleConfirmShutdown();   break;
    }
}

/* 进入图形模式：上半部显示显卡基本信息，下半部绘制演示画面，按键后返回文本模式 */
#define MOUSE_POINTER_W 10
#define MOUSE_POINTER_H 10

/* 鼠标箭头位图(进图形模式时加载一次, 不在每帧读盘) */
static BmpImage gMouseArrow;
static bool gHasMouseArrow = false;

/* 绘制鼠标指针: 有箭头位图用之, 否则回退"白方块+黑边" */
static void drawMousePointer(int x, int y) {
    if (gHasMouseArrow) {
        if (x < vbeWidth && y < vbeHeight)
            vbeDrawBitmap(x, y, gMouseArrow.pixels, gMouseArrow.w, gMouseArrow.h);
        return;
    }
    for (int iy = 0; iy < MOUSE_POINTER_H; iy++) {
        for (int ix = 0; ix < MOUSE_POINTER_W; ix++) {
            bool border = (ix == 0 || ix == MOUSE_POINTER_W - 1 ||
                           iy == 0 || iy == MOUSE_POINTER_H - 1);
            vbeSetColor(border ? vbeColor(0, 0, 0) : vbeColor(255, 255, 255));
            int px = x + ix, py = y + iy;
            if (px >= 0 && px < vbeWidth && py >= 0 && py < vbeHeight)
                vbeDrawPixel((uint32_t)px, (uint32_t)py);
        }
    }
}

/* 指针矩形物理尺寸: 有箭头位图则用其宽高, 否则用内置方块尺寸 */
static void pointerSize(int* w, int* h) {
    if (gHasMouseArrow) { *w = (int)gMouseArrow.w; *h = (int)gMouseArrow.h; }
    else                { *w = MOUSE_POINTER_W;   *h = MOUSE_POINTER_H;   }
}

/* 把场景快照 scene(RAM, bg+窗口 无指针)中 rect 区域拷贝回 LFB：
 * 用于"移动指针时抹掉旧指针"——只写一个矩形而非整帧。
 * 坐标越界自动裁剪到屏幕内。 */
static void blitSceneRect(const uint32_t* scene, int x, int y, int w, int h) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > vbeWidth)  x1 = vbeWidth;
    if (y1 > vbeHeight) y1 = vbeHeight;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t* lfb = (uint32_t*)(uintptr_t)gVbeInfo.lfbAddr;
    int bw = x1 - x0, bh = y1 - y0;
    gWMWriteOps++;
    gWMWritePix += (uint32_t)(bw * bh);
    for (int yy = 0; yy < bh; yy++) {
        const uint32_t* s = &scene[(y0 + yy) * vbeWidth + x0];
        uint32_t* d = &lfb[(y0 + yy) * vbeWidth + x0];
        for (int xx = 0; xx < bw; xx++) d[xx] = s[xx];
    }
}

/* 整帧把场景快照写回 LFB(窗口位置/内容大范围变化时才用) */
static void blitSceneFull(const uint32_t* scene, size_t pix) {
    memcpy((void*)(uintptr_t)gVbeInfo.lfbAddr, scene, pix * 4);
    gWMWriteOps++;
    gWMWritePix += (uint32_t)pix;
}

/* 演示窗口内容回调(客户区以绝对屏幕坐标绘制) */
static void wmTermDraw(Window* w) {
    int cx = w->x + 8;
    int ty = w->y + WM_TITLEBAR_H + 8;
    vbeSetColor(vbeColor(210, 255, 210));
    vbeDrawStringCJK(cx, ty, "VortexOS Terminal");
    vbeSetColor(vbeColor(140, 255, 140));
    vbeDrawStringCJK(cx, ty + 22, "> hello, window!");
    vbeDrawStringCJK(cx, ty + 44, "> dir");
    vbeDrawStringCJK(cx, ty + 66, "  system/  usr/  apps/");
}

static void wmAboutDraw(Window* w) {
    int cx = w->x + 8;
    int ty = w->y + WM_TITLEBAR_H + 8;
    vbeSetColor(vbeColor(30, 30, 30));
    vbeDrawStringCJK(cx, ty, "VortexOS 窗口管理器");
    vbeDrawStringCJK(cx, ty + 22, "版本 0.1");
    vbeDrawStringCJK(cx, ty + 44, "支持: 拖拽 / 聚焦 / 最小化 / 关闭");
    vbeDrawStringCJK(cx, ty + 66, "按 ESC 返回文本主菜单");
}

void graphic_main(unsigned int magic, unsigned int addr) {
    (void)magic; (void)addr;

    /* 静态画面构建期间关中断：构建全部走 RAM 缓冲与整帧写 LFB，保持原子。 */
    __asm__ volatile ("cli");

    serialPutStr("[G] enter\n");

    if (vbeSetMode(vbeWidth, vbeHeight, 32) != 0) {
        vgaClear();
        messageBox("Can't enter graphical mode\n");
        return;
    }
    serialPutStr("[G] vbeSetMode ok\n");

    BmpImage wallpaper;
    bool hasWallpaper = (bmpLoad("/system/images/wallpaper.bmp", &wallpaper) == 0);
    if (hasWallpaper) { serialPutStr("[G] wallpaper ok\n"); }
    else              { serialPutStr("[G] wallpaper fail\n"); }

    BmpImage taskbar;
    bool hasTaskbar = (bmpLoad("/system/images/taskbar.bmp", &taskbar) == 0);
    if (hasTaskbar) { serialPutStr("[G] taskbar ok\n"); }
    else            { serialPutStr("[G] taskbar fail\n"); }

    /* 加载鼠标箭头位图(仅一次, 供指针绘制复用) */
    gHasMouseArrow = (bmpLoad("/system/images/mousePointer/arrow.bmp", &gMouseArrow) == 0);

    /* 持久背景缓冲 bg：清黑+壁纸+任务栏+中文 的一次性渲染结果。
     * 注意：QEMU bochs VBE 下真实 LFB 可写不可回读，直接 shadow[i]=lfb[i] 会
     * 因读 LFB 页错误崩溃。故建立 bg(RAM) 仅作底，每帧整帧重合成到 shadow 再写 LFB。 */
    size_t bgPix = (size_t)vbeWidth * (size_t)vbeHeight;
    uint32_t* bg = (uint32_t*)pmmAllocPages((uint32_t)((bgPix * 4 + 0xFFF) >> 12));
    serialPutStr("[G] bg alloc\n");

    for (size_t i = 0; i < bgPix; i++) bg[i] = 0;
    vbeBeginRamFrame(bg);
    if (hasWallpaper)
        vbeDrawBitmap(0, 0, wallpaper.pixels, wallpaper.w, wallpaper.h);
    if (hasTaskbar)
        vbeDrawBitmap(0, vbeHeight - taskbar.h, taskbar.pixels, taskbar.w, taskbar.h);
    vbeSetColor(vbeColor(255, 255, 255));
    vbeDrawStringCJK(20, 20, "VortexOS 操作系统");
    vbeEndRamFrame();
    serialPutStr("[G] bg drawn\n");

    /* 释放不再组成的像素缓冲，降低内存占用 */
    if (hasWallpaper) free(wallpaper.pixels);
    if (hasTaskbar)   free(taskbar.pixels);

    /* 初始化窗口管理器 */
    wmInit(bg, vbeWidth, vbeHeight);
    /* 创建后再经 wmSetBodyColor 自定义背景色, 演示"窗口背景可运行时配置"：
     * Terminal 用深色底让浅绿文本可读, Notepad 白底, About 浅灰底。 */
    int wTerm = wmCreate("Terminal", 60, 70, 380, 240, vbeColor(60, 60, 180), vbeColor(255, 255, 255), wmTermDraw);
    int wNote = wmCreate("Notepad", 440, 120, 380, 260, vbeColor(40, 130, 70), vbeColor(255, 255, 255), NULL);
    int wAbout = wmCreate("About", 200, 300, 360, 180, vbeColor(150, 95, 45), vbeColor(255, 255, 255), wmAboutDraw);
    wmSetBodyColor(wTerm,  vbeColor(24, 24, 24));   /* 深色终端底 */
    wmSetBodyColor(wNote,  vbeColor(255, 255, 255));/* 白色记事本底 */
    wmSetBodyColor(wAbout, vbeColor(238, 238, 238));/* 浅灰关于底 */
    (void)wTerm; (void)wNote; (void)wAbout;
    serialPutStr("[WM] count=0x");
    serialPutHex8((uint8_t)wmWindowCount());
    serialPutStr(" windows\n");

    /* 合成缓冲：每帧把 bg+可见窗口画入 shadow，再整帧写回 LFB(纯写安全) */
    uint32_t* shadow = (uint32_t*)pmmAllocPages((uint32_t)((bgPix * 4 + 0xFFF) >> 12));
    serialPutStr("[G] shadow alloc\n");
    /* 拖动期间的"静止背景"缓冲：bg+除被拖窗口外所有窗口，拖动开始构建一次 */
    uint32_t* dragBack = (uint32_t*)pmmAllocPages((uint32_t)((bgPix * 4 + 0xFFF) >> 12));

    /* ---- 帧率/写屏统计 ----
     * 用 RTC 秒作为时钟(不触碰系统 PIT，避免影响任务调度)：每累计 1 秒输出
     * 一次平均帧率，以及该秒内 LFB 总写入像素数与 blit 次数，直观反映拖动时
     * 的写屏开销是否减少(条带刷新 vs 整帧写)。 */
    uint32_t gFpsFrames = 0;   /* 当前统计周期内渲染帧数 */
    RtcTime fpsT0, fpsT1;
    rtcGetTime(&fpsT0);
    gWMWritePix = 0;
    gWMWriteOps = 0;

    /* 初始化鼠标: 绑定屏幕尺寸并居中 */
    mouseSetBounds(vbeWidth, vbeHeight);
    mouseSetPosition(vbeWidth / 2, vbeHeight / 2);
    serialPutStr("[G] mouse set\n");

    /* 事件驱动阶段需要接收 IRQ12/键盘，打开中断(静态构建已原子完成) */
    __asm__ volatile ("sti");

    int dragWm = -1;
    int offx = 0, offy = 0;
    uint8_t prevButtons = 0;
    int nx = mouseGetX();
    int ny = mouseGetY();
    int lastX = nx, lastY = ny;
    int pw = 0, ph = 0;
    pointerSize(&pw, &ph);
    uint32_t frameTick = 0;

    /* 拖动脏矩形：记录窗口"上一帧位置"，拖动每帧只需重写 旧矩形∪新矩形 */
    int dragPrevX = 0, dragPrevY = 0, dragPrevW = 0, dragPrevH = 0;

    /* 首次渲染：合成场景(bg+窗口, 无指针)到 shadow，整帧写回 LFB，再画指针在中央 */
    wmComposite(shadow);
    blitSceneFull(shadow, bgPix);
    drawMousePointer(nx, ny);
    serialPutStr("[G] first frame drawn\n");

    for (;;) {
        /* 事件驱动: 无鼠标/键盘事件时让出 CPU(hlt 等待中断) */
        while (!mouseHasEvent() && !keyboardHasChar())
            __asm__ volatile ("hlt");

        /* 键盘: ESC 退出图形模式返回文本主菜单 */
        if (keyboardHasChar()) {
            char c = keyboardGetChar();
            if (c == KEY_ESC) break;
        }

        /* 一轮唤醒把队列里的鼠标事件合并处理，产出最终状态；
         * sceneDirty=true 表示窗口发生移动/关闭/最小化/聚焦，需要整帧重合成。 */
        bool sceneDirty = false;
        do {
            nx = mouseGetX();
            ny = mouseGetY();
            uint8_t btn = mouseGetButtons();

            /* 取出并清掉累积位移与 pending 标志(绝对坐标已由中断更新) */
            int moveDx = 0, moveDy = 0;
            mouseGetMotion(&moveDx, &moveDy);
            (void)moveDx; (void)moveDy;

            if (btn & MOUSE_LEFT_BUTTON) {
                if (!(prevButtons & MOUSE_LEFT_BUTTON)) {
                    /* 按下瞬间: 命中测试并分发 */
                    int idx = wmHitTest(nx, ny);
                    if (idx != -1) {
                        int action = wmHitAction(idx, nx, ny);
                        if (action == WM_ACT_CLOSE) {
                            wmClose(idx);
                            sceneDirty = true;
                        } else if (action == WM_ACT_MINIMIZE) {
                            wmMinimize(idx);
                            sceneDirty = true;
                        } else if (action == WM_ACT_DRAG) {
                            idx = wmFocus(idx);              // 置顶聚焦, 取新索引
                            wmGetRect(idx, &dragPrevX, &dragPrevY,
                                      &dragPrevW, &dragPrevH); /* 记录拖动起点矩形 */
                            offx = nx - dragPrevX;
                            offy = ny - dragPrevY;
                            /* 焦点置顶改变了遮挡关系, 整帧重画并写一次 LFB:
                             * 让 LFB 立即反映置顶后的正确画面, 否则条带刷新的
                             * "中心不变区不重写"会残留旧遮挡轮廓(残影)。 */
                            wmComposite(shadow);
                            blitSceneFull(shadow, bgPix);
                            /* 构建拖动期间的"静止背景"：bg+除本窗口外所有窗口。
                             * 拖动中窗口内容不变, 每帧只需把本窗口叠画到该 back 上。 */
                            wmCompositeExcluding(dragBack, idx);
                            dragWm = idx;                    // 拖动走条带刷新路径
                        } else {
                            wmFocus(idx);                    // 客户区: 仅聚焦
                            sceneDirty = true;
                        }
                    }
                }
            } else if (prevButtons & MOUSE_LEFT_BUTTON) {
                /* 松开: 结束拖拽。置 sceneDirty 让下一次渲染整帧对齐,
                 * 避免松开前合并进同一批的位移未刷导致窗口错位。 */
                dragWm = -1;
                sceneDirty = true;
            }

            prevButtons = btn;
        } while (mouseHasEvent() || keyboardHasChar());

        /* 关键优化: 不在 while 内每个鼠标事件都 move 窗口。一次唤醒的多个位移
         * 若只在循环后渲染一次, 条带刷新只覆盖首尾位置、中间扫过的区域(露出
         * 背景/下层窗口)会漏刷 → 残影。故改为循环外只按最终鼠标位置单次移动,
         * 让条带精确匹配「单次净位移」, 彻底消除中间漏刷。 */
        if (dragWm != -1)
            wmMove(dragWm, nx - offx, ny - offy);

        if (dragWm != -1) {
            /* 拖动窗口：把"静止背景"(back)拷贝到 shadow，再把被拖窗口叠画上去，
             * 最后只重写「旧位置∪新位置」的包围盒矩形到 LFB。
             * 采用整体包围盒而非 L 形条带：条带接缝/中间位移在少数几何下会
             * 漏刷导致残影与错乱, 包围盒单次矩形刷新写屏略多但绝对无漏洞。 */
            int curX, curY, curW, curH;
            wmGetRect(dragWm, &curX, &curY, &curW, &curH);
            /* 局部恢复旧位置区域: 只把上一帧拖窗所在矩形重置为该处的背景/下层
             * 窗口(dragBack 内容), 而不是整帧 memcpy 全 3MB。shadow 一直维护为
             * 完整当前场景, 故逐行拷贝窗口大小的行片段即可。 */
            for (int rp = dragPrevY; rp < dragPrevY + dragPrevH; rp++) {
                if (rp < 0 || rp >= vbeHeight) continue;
                memcpy(&shadow[(size_t)rp * vbeWidth + dragPrevX],
                       &dragBack[(size_t)rp * vbeWidth + dragPrevX],
                       (size_t)dragPrevW * 4);
            }
            wmCompositeOnly(shadow, dragWm);
            int bMinX = dragPrevX < curX ? dragPrevX : curX;
            int bMinY = dragPrevY < curY ? dragPrevY : curY;
            int bMaxX = (dragPrevX + dragPrevW) > (curX + curW)
                       ? (dragPrevX + dragPrevW) : (curX + curW);
            int bMaxY = (dragPrevY + dragPrevH) > (curY + curH)
                       ? (dragPrevY + dragPrevH) : (curY + curH);
            /* 半开区间右/下界不含最外一行像素, 而窗口右/下边框恰好落在
             * 该行; 故右/下各外扩 1 像素, 保证边框线被完整重写覆盖(blitSceneRect
             * 内部会 clamp 到屏幕, 越界来源是 shadow 的相邻场景, 无害)。 */
            blitSceneRect(shadow, bMinX, bMinY,
                          (bMaxX - bMinX) + 1, (bMaxY - bMinY) + 1);
            /* 抹掉上一帧指针残留：拖动帧把 shadow 重置为 back(不含指针),
             * 若指针落在包围盒外不会被覆盖, 显式恢复其所在矩形。 */
            blitSceneRect(shadow, lastX, lastY, pw, ph);
            drawMousePointer(nx, ny);
            dragPrevX = curX;
            dragPrevY = curY;
            dragPrevW = curW;
            dragPrevH = curH;
            lastX = nx;
            lastY = ny;
        } else if (sceneDirty) {
            /* 关闭/最小化/聚焦(非拖动)：重合成整个场景，整帧写 LFB，再画指针 */
            wmComposite(shadow);
            blitSceneFull(shadow, bgPix);
            drawMousePointer(nx, ny);
        } else if (nx != lastX || ny != lastY) {
            /* 仅指针移动：从场景快照抹掉"旧指针"矩形，再画新指针。
             * 只写两个小矩形，避免整帧 3MB 写屏(这是 QEMU 卡顿主因)。 */
            blitSceneRect(shadow, lastX, lastY, pw, ph);
            drawMousePointer(nx, ny);
            lastX = nx;
            lastY = ny;
        }

        /* 帧率/写屏统计：每帧记 1, 每秒用 RTC 秒差输出一次。
         * 仅当该秒确实发生了 LFB 写入才打印, 空闲时不刷屏。 */
        gFpsFrames++;
        rtcGetTime(&fpsT1);
        if (fpsT1.second != fpsT0.second) {
            if (gWMWriteOps > 0) {
                serialPutStr("[FPS] ");
                serialPutDec32(gFpsFrames);
                serialPutStr("fps pix=");
                serialPutDec32(gWMWritePix);
                serialPutStr(" ops=");
                serialPutDec32(gWMWriteOps);
                serialPutStr("\n");
            }
            gFpsFrames = 0;
            gWMWritePix = 0;
            gWMWriteOps = 0;
            fpsT0 = fpsT1;
        }

        /* 节流心跳: 每 500 帧打印一次, 证明事件循环在持续运转 */
        if ((++frameTick % 500) == 0) {
            serialPutStr("[G] tick\n");
        }
    }
}

void kernel_main(unsigned int magic, unsigned int addr) {
    (void)magic; (void)addr;

    serialPutStr("VortexOS\n");

    theme = VGA_COLOR(FG, BG);
    HL = vgaEntryColor(COLOR_LIGHT_BLUE, BG);
    LL = vgaEntryColor(COLOR_LIGHT_GREY, BG);

    vgaPutStr("[GDT] Initialized\n");
    serialPutStr("[GDT] Initialized\n");

    pmmInit(256 * 1024 * 1024);  // 256MB PMM 最先初始化
    pagingInit();
    vgaPutStr("[PMM] Initialized\n");
    vgaPutStr("[PAGING] Initialized\n");
    serialInit();
    vgaPutStr("[SERIAL] Initialized\n");

    static uint8_t kernelStack[4096] __attribute__((aligned(16)));
    tssInit((uint32_t)kernelStack + sizeof(kernelStack));
    vgaPutStr("[TSS] Initialized\n");
    serialPutStr("[TSS] Initialized\n");

    serialPutStr("[PMM] Initialized\n");// 调试信息
    serialPutStr("[PAGING] Initialized\n");// 调试信息
    serialPutStr("[SERIAL] Initialized\n");// 调试信息

    taskInit();
    vgaPutStr("[TASK] Initialized\n");
    serialPutStr("[TASK] Initialized\n");// 调试信息

    vgaInit();
    vgaPutStr("[VGA] Initialized\n");
    serialPutStr("[VGA] Initialized\n");// 调试信息
    idtInit();
    vgaPutStr("[IDT] Initialized\n");
    serialPutStr("[IDT] Initialized\n");// 调试信息
    syscallInit();
    vgaPutStr("[SYSCALL] Initialized\n");
    serialPutStr("[SYSCALL] Initialized\n");// 调试信息
    vgaPutStr("[EXCEPTIONS] Initialized\n");
    serialPutStr("[EXCEPTIONS] Initialized\n");// 调试信息
    keyboardInit();
    vgaPutStr("[KEYBOARD] Initialized\n");
    serialPutStr("[KEYBOARD] Initialized\n");// 调试信息
    mouseInit();
    serialPutStr("[MOUSE] Initialized\n");

    /* 显卡信息探测：分辨率/色深/显存在 graphic 菜单与 Device Info 中使用 */
    if (vbeInit() == 0) {
        vbeSyncInfo();
        serialPutStr("[VBE] init ok\n");
        vgaPutStr("[VBE] init ok\n");
    } else {
        serialPutStr("[VBE] init failed\n");
        vgaPutStr("[VBE] init failed\n");
    }

    #if 0 /* USB 开发暂停（XHCI 尚未跑通）：临时摘除，避免阻塞启动。恢复后改回 #if 1 */
    /* USB XHCI（输入）中断驱动初始化；HID 键盘注入现有输入管线 */
    serialPutStr("[USB] probe start\n");
    vgaPutStr("[USB] probe start\n");
    if (xhciInit() == 0) {
        hidLoadFromXhci();
        serialPutStr("[USB] input ready\n");
        vgaPutStr("[USB] input ready\n");
    } else {
        serialPutStr("[USB] XHCI init failed, PS/2 fallback\n");
        vgaPutStr("[USB] XHCI init failed, PS/2 fallback\n");
    }
    #endif

    /* 光驱(ATAPI)初始化：供安装系统/字体加载读取 CD-ROM */
    atapiInit();

    if (fat32Init(&fsVolume)) {
        vgaPutStr("[FAT32] Initialized\n");
        serialPutStr("[FAT32] Initialized\n");// 调试信息
    } else {
        vgaPutStr("[FAT32] Init failed\n");
        serialPutStr("[FAT32] Init failed\n");// 调试信息
    }

    /* 交互式安装/更新向导(文本模式)。
     * 向导内需要处理键盘输入，故先开中断(idt/keyboard 已初始化)。
     *   硬盘未格式化 -> 询问"格式化安装"或"从 CD 运行"
     *   已格式化且 CD 系统不同 -> 询问是否更新；一致则直接继续
     * 安装/更新完成会在向导内自动重启。 */
    __asm__ volatile ("sti");
    InstallResult installResult = installWizard();
    __asm__ volatile ("cli");

    if (installResult == INST_RESULT_RUN_CD) {
        /* 本轮从 CD-ROM 运行：不写盘；字体直接读自光驱加载进 VBE */
        serialPutStr("[INSTALL] Running from CD-ROM this round\n");
        vgaPutStr("[INSTALL] Running from CD-ROM this round\n");
        loadFontFromCdIntoVbe();
        /* 文本模式(Shell/菜单)字体同样从光驱上传到 VGA 字模平面 */
        loadFontFromCdIntoVga();
    } else { /* INST_RESULT_BOOT */
        /* 每次开机都从光驱刷新硬盘字体，避免旧/残缺字体残留导致图形文字乱码。
         * loadFontFromCd() 会无条件覆写 /system/font/font.bin；CD 读取失败则不更动。 */
        if (fsVolume.valid) {
            loadFontFromCd();
            verifyFontOnDisk();
        }
        /* 把字体读进内存并注册给 VBE，供图形模式 8x16 渲染 */
        loadFontIntoVbe();
        /* 中文 16x16 字库也注册给 VBE，供 vbeDrawStringCJK 渲染 */
        loadCjkFontIntoVbe();
        /* 文本模式(Shell/菜单)字体：上传到 VGA 字模平面 */
        loadFontIntoVga();
    }
    
    __asm__ volatile ("fninit");  // 初始化 FPU
    vgaPutStr("[FPU] Initialized\n");
    serialPutStr("[FPU] Initialized\n");// 调试信息

    

    //__asm__ volatile ("hlt");

    vgaDisableCursor();
    vgaSetColorByte(theme);
    vgaClear();

    __asm__ volatile ("sti");
    serialPutStr("Enable Interrupt\n");

    drawMainMenu();

    for (;;) {
        if (keyboardHasChar()) {
            unsigned char c = keyboardGetChar();
            int size = getCurrentSize();

            if (c == KEY_ESC) {
                if (!popMenu()) {
                    // 已经在主菜单，不做任何事
                }
                drawCurrentMenu();
            } else if (c == KEY_UP || c == 'w' || c == 'W') {
                serialPutStr("up\n");
                if (--selectedIndex < 0)
                    selectedIndex = size - 1;
                drawCurrentMenu();
            } else if (c == KEY_DOWN || c == 's' || c == 'S') {
                serialPutStr("down\n");
                if (++selectedIndex >= size)
                    selectedIndex = 0;
                drawCurrentMenu();
            } else if (c == '\r' || c == '\n' || c == ' ') {
                serialPutStr("ok\n");
                selectIndex = selectedIndex;
                handleSelect();
            }
        }
        __asm__ volatile ("hlt");
    }
}