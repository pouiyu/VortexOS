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
#include <disk.h>
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
#include <wm/wmsvc.h>
#include "elf/elf.h"
#include <rtc.h>

uint8_t FG = COLOR_WHITE;
uint8_t BG = COLOR_BLACK;
uint8_t HL ;
uint8_t LL ;
uint8_t theme;

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

    uint8_t color = hover ? vgaInvertColor(theme) : theme;
    vgaSetColorByte(color);

    /* 一次填满本行剩余列(批量字符串, 只触发一次 vgaOutputFlush)。
     * 旧的 `for(...) vgaPutChar(' ')` 逐字符各触发一次整屏 diff,
     * 帧缓冲模式下一个菜单重绘会累积几百次刷新, 卡成'逐字符打字'。 */
    int remaining = VGA_WIDTH - col;
    if (remaining > 0) {
        static char pad[VGA_WIDTH + 1];
        int i = 0;
        while (i < remaining) pad[i++] = ' ';
        pad[i] = '\0';
        vgaPutStrColor(pad, color);
    }

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

/* 用户态 GUI 程序退出(SYS_EXIT → sysExitKernel)后的内核续点：
 * 恢复文本模式并重绘主菜单，随后正常返回到 graphic_main 的调用方
 * (handleMainMenuSelect 会再刷新一次菜单，等价于旧 ESC 退出路径)。 */
static void guiExitToMenu(void) {
    vbeDisable();
    drawMainMenu();
}

/* 整帧把场景快照写回 LFB(窗口位置/内容大范围变化时才用) */
static void blitSceneFull(const uint32_t* scene, size_t pix) {
    memcpy((void*)(uintptr_t)gVbeInfo.lfbAddr, scene, pix * 4);
}

/* ============ Multiboot2 帧缓冲(Tag 8)解析(前向声明) ============ */

typedef struct {
    uint32_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} MbFbInfo;

/* 启动时保存的 Multiboot2 信息物理地址(GRUB 的 ebx)。启动阶段不切图形模式，
 * 仅当用户从菜单选 Graphic 时，graphic_main 用它在真机上按需采用引导器帧缓冲。 */
static unsigned int gMb2Info = 0;

static int mb2FindFramebuffer(uint32_t infoAddr, MbFbInfo* out);

void graphic_main(unsigned int magic, unsigned int addr) {
    (void)magic;

    /* 静态画面构建期间关中断：构建全部走 RAM 缓冲与整帧写 LFB，保持原子。 */
    __asm__ volatile ("cli");

    serialPutStr("[G] enter\n");

    /* 真机显卡无 Bochs dispi 端口：若 GRUB 提供了帧缓冲则按需采用之，
     * 之后再走 vbeSetMode(此时经 sFromBootloader 短路复用该帧缓冲)。 */
    if (!vbeReady()) {
        MbFbInfo fb;
        if (mb2FindFramebuffer(addr ? addr : gMb2Info, &fb) == 0 &&
            fb.addr && fb.width && fb.height && fb.bpp >= 8) {
            if (vbeUseBootloaderFramebuffer(fb.addr, fb.pitch, fb.width, fb.height, fb.bpp) == 0)
                serialPutStr("[G] use bootloader fb\n");
        }
    }

    if (vbeSetMode(vbeWidth, vbeHeight, 32) != 0) {
        /* 进入图形失败：必须立即返回。若继续往下走，blitSceneFull 会向
         * lfbAddr=0 做整帧 memcpy 直接页错误崩死，表现为真机"卡死在进图形"。
         * 失败原因统一写串口+屏幕(帧缓冲文本仍可显示)，由 kernel_main 回退文本菜单。 */
        serialPutStr("[G] vbeSetMode FAILED (no usable framebuffer)\n");
        vgaClear();
        vgaPutStr("Can't enter graphical mode (no framebuffer)\n");
        vgaOutputFlush();
        /* 进入本函数时已 cli；失败返回前必须重新开中断，否则回到静态文本菜单
         * (drawMainMenu)后键盘 IRQ 永不触发，表现为"点按键无响应/卡死"。 */
        __asm__ volatile ("sti");
        return;
    }
    serialPutStr("[G] vbeSetMode ok\n");

    /* 进入图形模式时才初始化 PS/2 鼠标。启动早期跳过是为避免老笔记本 DELL EC
     * 对 8042 命令口写(0x64)敏感挂起；此处系统已稳定且是用户主动进入图形，
     * 带快速超时的安全初始化即使失败也不阻塞(无 PS/2 鼠标时快速返回，键盘可退出)。 */
    mouseInit();

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
    vbeEndRamFrame();
    serialPutStr("[G] bg drawn\n");

    /* 释放不再组成的像素缓冲，降低内存占用 */
    if (hasWallpaper) free(wallpaper.pixels);
    if (hasTaskbar)   free(taskbar.pixels);

    /* 初始化窗口管理器 */
    wmInit(bg, vbeWidth, vbeHeight);
    /* 窗口由用户态 GUI 程序经 SYS_WM_CREATE 创建，此处不再创建演示窗口 */

    /* 合成缓冲：每帧把 bg+可见窗口画入 shadow，再整帧写回 LFB(纯写安全) */
    uint32_t* shadow = (uint32_t*)pmmAllocPages((uint32_t)((bgPix * 4 + 0xFFF) >> 12));
    serialPutStr("[G] shadow alloc\n");
    /* 拖动"静止背景"：bg+除被拖窗口外所有窗口, 按下拖动时构建一次。
     * 拖动期间其它窗口不动, 用该缓存直接拷贝, 避免每帧重画其文字(字体渲染是
     * QEMU 下的 CPU 大头), 是拖动流畅的关键。 */
    uint32_t* dragBack = (uint32_t*)pmmAllocPages((uint32_t)((bgPix * 4 + 0xFFF) >> 12));

    /* 初始化鼠标: 绑定屏幕尺寸并居中 */
    mouseSetBounds(vbeWidth, vbeHeight);
    mouseSetPosition(vbeWidth / 2, vbeHeight / 2);
    serialPutStr("[G] mouse set\n");

    /* 事件驱动阶段需要接收 IRQ12/键盘，打开中断(静态构建已原子完成) */
    __asm__ volatile ("sti");

    /* 首次渲染：合成场景(bg+窗口, 无指针)到 shadow，整帧写回 LFB，再画指针在中央 */
    wmComposite(shadow);
    blitSceneFull(shadow, bgPix);
    drawMousePointer(mouseGetX(), mouseGetY());
    serialPutStr("[G] first frame drawn\n");

    /* WM 服务：输入事件循环(拖拽/关闭/最小化/聚焦)、场景合成、LFB 回写与
     * 鼠标指针绘制全部收敛到 wmsvc，替代旧的内联事件循环。 */
    wmsvcInit(shadow, dragBack, bgPix);
    if (gHasMouseArrow)
        wmsvcSetArrow(gMouseArrow.pixels, gMouseArrow.w, gMouseArrow.h);

    /* 加载用户态 GUI 程序(ELF)并跳转执行。之后窗口的创建/绘制/输入全部由
     * 用户程序经 SYS_WM_* 系统调用驱动；程序退出(SYS_EXIT)时经 sysExitKernel
     * 回到 guiExitToMenu 恢复文本主菜单。 */
    serialPutStr("[G] loading prog\n");
    uint32_t guiEntry = elfLoad("/system/programs/My_UI.elf");
    if (!guiEntry) {
        serialPutStr("load failed prog\n");
        vbeDisable();
        return;   /* 加载失败: 回文本菜单 */
    }
    serialPutStr("[G] jump to prog\n");
    jumpToUserGui((void*)guiEntry, guiExitToMenu);
    /* 不会到达 */
}

/* ============ Multiboot2 帧缓冲(Tag 8)解析 ============ */

/* 解析 Multiboot2 信息结构，在 (base + off) 的 tag 链表里找 framebuffer tag(type 8)，
 * 回填帧缓冲参数。找不到返回 -1。infoAddr 为引导器传入的 ebx(Multiboot2 信息指针)。 */
static int mb2FindFramebuffer(uint32_t infoAddr, MbFbInfo* out) {
    if (infoAddr == 0) return -1;
    uint32_t total = *(const uint32_t*)(uintptr_t)infoAddr;   /* total_size */
    uint32_t off   = 8;                                       /* 头 8 字节之后为 tag 链表 */
    while (off + 8 <= total) {
        const uint8_t* p  = (const uint8_t*)(uintptr_t)(infoAddr + off);
        uint32_t type = *(const uint32_t*)(p);
        uint32_t size = *(const uint32_t*)(p + 4);
        if (size < 8 || off + size > total) break;
        if (type == 8 && size >= 32) {
            const uint8_t* f = p + 8;
            uint8_t fbtype = *(const uint8_t*)(f + 21);
            serialPutStr("[MB2] fb tag type=");
            serialPutHex8(fbtype);
            serialPutStr(" addr=");
            serialPutHex32((uint32_t)(*(const uint64_t*)(f)));
            serialPutStr("\n");
            /* Multiboot2 framebuffer tag: addr@0, pitch@8, width@12, height@16,
             * bpp@20, framebuffer_type@21。
             * 只接受真正的线性 RGB 像素帧缓冲。GRUB 在显卡不支持 VBE 时会把
             * 文本模式"帧缓冲"(type=2 误标, addr=0xB8000, 80x25)报上来；此前
             * 把它当像素帧缓冲：vgaOutputFlush 把每个文本单元按 8x16"像素"
             * 重写回 0xB8000，屏上便出现白色大字残影(VORTEX0)且逐格重绘卡顿。
             * 像素帧缓冲至少 320x200，且地址不会落在 VGA 文本窗口(0xA0000-
             * 0xBFFFF)内。 */
            if (fbtype != 2) return -1;
            out->addr   = (uint32_t)(*(const uint64_t*)(f));      /* framebuffer_addr 低 32 位 */
            out->pitch  = *(const uint32_t*)(f + 8);
            out->width  = *(const uint32_t*)(f + 12);
            out->height = *(const uint32_t*)(f + 16);
            out->bpp    = *(const uint8_t*)(f + 20);
            if (out->width < 320 || out->height < 200) return -1;
            if (out->addr >= 0xA0000u && out->addr <= 0xBFFFFu) return -1;
            return 0;
        }
        off += size;
        if (size & 7) off += 8 - (size & 7);   /* tag 按 8 字节对齐 */
    }
    return -1;
}

/* ============ 开机文本启动画面 ============
 * 把开机初期那次性初始化进度渲染成一个"横幅 + 状态列表 + 底部进度条"的
 * 干净画面：顶部蓝底横幅带系统名与版本，中部逐模块列出初始化步骤(左侧
 * 亮色标签 + 右侧绿色 OK)，底部一条随模块推进的进度条。真机上经
 * vgaSetFramebufferOutput 差异刷到 GRUB 帧缓冲，同样生效。 */

#define BOOT_MODULE_ROW   5     /* 状态列表起始行 */
#define BOOT_MODULE_LAST  20    /* 状态列表末尾行(行 5..20 = 16 个槽位) */
#define BOOT_PROGBAR_ROW  22    /* 进度条所在行 */
#define BOOT_FOOTER_ROW   24    /* 底部就绪提示行 */
#define BOOT_BAR_COL      12
#define BOOT_BAR_W        52

static int     bootModuleRow = BOOT_MODULE_ROW;
static int     bootTotal = 16;
static int     bootDone = 0;
static uint8_t bootTagColor;
static uint8_t bootOkColor;

/* 重画底部进度条 + 百分比：空底用深蓝，已填充段用亮青。 */
static void bootProgress(void) {
    int pct  = bootTotal ? bootDone * 100 / bootTotal : 100;
    int full = pct * BOOT_BAR_W / 100;

    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL - 9);
    vgaPutStrColor("Loading:", bootTagColor);

    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL - 1);
    vgaPutCharColor('[', bootTagColor);
    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL);
    for (int i = 0; i < BOOT_BAR_W; i++)
        vgaPutCharColor(' ', vgaEntryColor(COLOR_BLACK, COLOR_BLUE));
    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL + 1);
    for (int i = 0; i < full - 2 && i < BOOT_BAR_W; i++)
        vgaPutCharColor(' ', vgaEntryColor(COLOR_BLACK, COLOR_LIGHT_CYAN));
    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL + BOOT_BAR_W);
    vgaPutCharColor(']', bootTagColor);

    /* 百分比(右对齐到 % ) */
    char pctBuf[4];
    pctBuf[0] = '0' + pct / 100;
    pctBuf[1] = '0' + (pct / 10) % 10;
    pctBuf[2] = '0' + pct % 10;
    pctBuf[3] = '\0';
    vgaSetCursorPos(BOOT_PROGBAR_ROW, BOOT_BAR_COL + BOOT_BAR_W + 3);
    vgaPutStrColor(pctBuf, vgaEntryColor(COLOR_LIGHT_GREY, BG));
    vgaPutCharColor('%', vgaEntryColor(COLOR_LIGHT_GREY, BG));
}

/* 画中间横幅(两行蓝底 + 一行亮条)并复位状态。 */
static void bootInit(void) {
    bootModuleRow = BOOT_MODULE_ROW;
    bootDone = 0;
    bootTotal = 16;
    bootTagColor = vgaEntryColor(COLOR_WHITE, BG);
    bootOkColor  = vgaEntryColor(COLOR_GREEN, BG);

    vgaDisableCursor();
    vgaClear();

    vgaSetCursorPos(0, 0);
    for (int i = 0; i < VGA_WIDTH; i++)
        vgaPutCharColor(' ', vgaEntryColor(COLOR_WHITE, COLOR_BLUE));

    /* 蓝色横幅区(第1行)不再绘制 "VortexOS Operating System" 品牌文字 */

    vgaSetCursorPos(2, 0);
    for (int i = 0; i < VGA_WIDTH; i++)
        vgaPutCharColor(' ', vgaEntryColor(COLOR_LIGHT_CYAN, COLOR_BLUE));

    vgaSetCursorPos(3, 4);
    vgaPutStrColor("Initializing system components...", vgaEntryColor(COLOR_LIGHT_GREY, BG));

    bootProgress();
    vgaOutputFlush();
}

/* 记录一个已完成的初始化步骤：在列表中画一行"[NAME]" + 右侧绿色"[ OK ]"，并推进进度条。 */
static void bootModule(const char* name) {
    if (bootModuleRow > BOOT_MODULE_LAST) bootModuleRow = BOOT_MODULE_LAST;
    int row = bootModuleRow++;

    vgaSetCursorPos(row, 4);
    vgaPutCharColor('[', bootTagColor);
    vgaPutStrColor(name, bootTagColor);
    vgaPutCharColor(']', bootTagColor);
    vgaSetCursorPos(row, 72);
    vgaPutStrColor("[ OK ]", bootOkColor);

    bootDone++;
    bootProgress();
    vgaOutputFlush();
}

/* 全部初始化完成后：进度条拉满并显示底部就绪提示。 */
static void bootFinish(void) {
    bootDone = bootTotal;
    bootProgress();
    vgaSetCursorPos(BOOT_FOOTER_ROW, 28);
    vgaPutStrColor("System loaded. Booting desktop...", bootOkColor);
    vgaOutputFlush();
}

void kernel_main(unsigned int magic, unsigned int addr) {
    (void)magic;   /* addr 保存到 gMb2Info，供 graphic_main 按需采用引导器帧缓冲 */

    serialPutStr("VortexOS\n");

    theme = VGA_COLOR(FG, BG);
    HL = vgaEntryColor(COLOR_LIGHT_BLUE, BG);
    LL = vgaEntryColor(COLOR_LIGHT_GREY, BG);

    /* 帧缓冲初始化需要分页(PMM/PAGING)先就绪，故提前到启动画面之前：
     * - 引导器(GRUB)帧缓冲检测后立即启用 vgaSetFramebufferOutput，使启动画面
     *   起所有文本都经 RAM 文本模型绘制，规避 GRUB 图形模式下 VGA 文本窗口
     *   0xB8000 的平面错乱(文字错位、白色大字残影)。 */
    gMb2Info = addr;
    pmmInit(256 * 1024 * 1024);  // 256MB PMM 最先初始化
    pagingInit();

    bool bootFb = false;
    {
        MbFbInfo fb;
        /* 文本渲染(vbeFbTextCell)与 bpp 无关，接受 8/15/16/24/32 任意色深的
         * GRUB 帧缓冲，保证真机显卡只给 24bpp 时也不会黑屏。32bpp 时还能进
         * 图形模式；非 32bpp 仅文本，vbeSetMode 会安全失败回文本菜单。 */
        if (mb2FindFramebuffer(gMb2Info, &fb) == 0 && fb.addr &&
            fb.width && fb.height && fb.bpp >= 8 &&
            vbeUseBootloaderFramebuffer(fb.addr, fb.pitch, fb.width, fb.height, fb.bpp) == 0) {
            bootFb = true;
        }
    }
    if (bootFb) vgaSetFramebufferOutput(true);

    /* 开机启动画面：横幅 + 模块状态 + 进度条 */
    bootInit();

    bootModule("GDT");
    serialPutStr("[GDT] Initialized\n");

    bootModule("PMM");
    bootModule("PAGING");
    serialInit();
    bootModule("SERIAL");

    static uint8_t kernelStack[4096] __attribute__((aligned(16)));
    tssInit((uint32_t)kernelStack + sizeof(kernelStack));
    bootModule("TSS");
    serialPutStr("[TSS] Initialized\n");

    serialPutStr("[PMM] Initialized\n");// 调试信息
    serialPutStr("[PAGING] Initialized\n");// 调试信息
    serialPutStr("[SERIAL] Initialized\n");// 调试信息

    taskInit();
    bootModule("TASK");
    serialPutStr("[TASK] Initialized\n");// 调试信息

    vgaInit();
    bootModule("VGA");
    serialPutStr("[VGA] Initialized\n");// 调试信息
    idtInit();
    bootModule("IDT");
    serialPutStr("[IDT] Initialized\n");// 调试信息
    syscallInit();
    bootModule("SYSCALL");
    serialPutStr("[SYSCALL] Initialized\n");// 调试信息
    bootModule("EXCEPTION");
    serialPutStr("[EXCEPTIONS] Initialized\n");// 调试信息
    keyboardInit();
    bootModule("KEYBOARD");
    serialPutStr("[KEYBOARD] Initialized\n");// 调试信息
    /* 不在启动时初始化 PS/2 鼠标。部分老笔记本(如 DELL/原 Win7)的 EC 对 OS
     * 写入 8042 KBC 命令口(0x64, 禁/开 aux、读命令字节)极度敏感，一旦触碰可
     * 触发 SMI/平台级挂起，整机冻结在 [KEYBOARD] 之后。文本菜单/安装向导用
     * 不到鼠标，鼠标 IRQ12 处理已随 IDT 挂好，故启动阶段跳过硬件探测以保证不卡死。 */
    serialPutStr("[MOUSE] skip (deferred)\n");
    bootModule("MOUSE");/* 屏幕进度标记：证明已越过键盘进入 ATAPI 阶段 */

    /* 注册 GRUB module2 送入内存的 system 文件(字体/壁纸/图形程序/安装源)，
     * 作为无 CD(U盘启动/光驱为 AHCI)与未格式化盘时的统一文件后备。 */
    fsRegisterModules(addr);

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
    bootModule("ATAPI");

    /* 磁盘抽象层：枚举 AHCI(SATA) 与 legacy IDE 硬盘(真机/VMware 可能只走其一) */
    diskInit();
    bootModule("DISK");

    /* 引导器帧缓冲已在启动画面前检测并启用(见 kernel_main 开头)，
     * 此处保留 DISPLAY 模块标记保持启动画面顺序不变。 */
    if (bootFb) {
        serialPutStr("[DISPLAY] use bootloader framebuffer\n");
        serialPutStr("[VBE] fb addr=");
        serialPutHex32(gVbeInfo.lfbAddr);
        serialPutStr(" res=");
        serialPutHex32(gVbeInfo.xres);
        serialPutStr("x");
        serialPutHex32(gVbeInfo.yres);
        serialPutStr(" bpp=");
        serialPutHex32(gVbeInfo.bpp);
        serialPutStr(" pitch=");
        serialPutHex32(gVbeInfo.pitch);
        serialPutStr("\n");
    }
    bootModule("DISPLAY");

    dDrive* bootDrv = (diskGetCount() > 0) ? diskGetDrive(0) : NULL;
    if (fat32Init(&fsVolume, bootDrv)) {
        serialPutStr("[FAT32] Initialized\n");// 调试信息
    } else {
        serialPutStr("[FAT32] Init failed\n");// 调试信息
    }
    bootModule("FAT32");

    /* fb 模式下，把字体加载进 VBE(文本/菜单绘制需要字形)；帧缓冲文本输出
     * 已在启动画面前启用，后续安装向导与主菜单直接显示在帧缓冲上。 */
    if (bootFb) {
        if (fsVolume.valid) { loadFontIntoVbe(); loadCjkFontIntoVbe(); }
        else                { loadFontFromCdIntoVbe(); }
    }

    /* 交互式安装/更新向导(文本模式)。
     * 向导内需要处理键盘输入，故先开中断(idt/keyboard 已初始化)。
     *   硬盘未格式化 -> 询问"格式化安装"或"从 CD 运行"
     *   已格式化且 CD 系统不同 -> 询问是否更新；一致则直接继续
     * 安装/更新完成会在向导内自动重启。 */
    bootFinish();   /* 初始化画面收尾：进度拉满并提示进入桌面 */
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

    /* 开机直接进入图形化系统：加载用户 GUI 程序并跳转。图形初始化失败或
     * 用户 GUI 程序退出时会 return 回到此处，再进入文本主菜单作为兜底。 */
    graphic_main(0, 0);

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