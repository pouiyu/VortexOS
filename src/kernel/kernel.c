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
#include "tss.h"
#include "syscall.h"
#include "user.h"
#include "task.h"
#include <pit.h>
#include <usb/xhci.h>
#include <usb/hid.h>
#include <stdio/vbe.h>
#include <fs/bmp.h>

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

/* 用快照缓冲 shadow 恢复 (x,y) 处 MOUSE_POINTER_W x MOUSE_POINTER_H 区域到 LFB。
 * shw 为 shadow 的一行像素数(屏幕宽度), 越界自动裁剪, 避免贴边越界写 LFB。 */
static void blitShadowToLfb(const uint32_t* shadow, int shw, int x, int y, int w, int h) {
    if (x < 0 || y < 0 || x >= vbeWidth || y >= vbeHeight || w <= 0 || h <= 0) return;
    if (x + w > vbeWidth)  w = vbeWidth  - x;
    if (y + h > vbeHeight) h = vbeHeight - y;
    uint32_t* lfb = (uint32_t*)(uintptr_t)gVbeInfo.lfbAddr;
    for (int yy = 0; yy < h; yy++) {
        const uint32_t* s = &shadow[(y + yy) * shw + x];
        uint32_t* d = &lfb[(y + yy) * vbeWidth + x];
        for (int xx = 0; xx < w; xx++) d[xx] = s[xx];
    }
}

/* 指针实际绘制尺寸: 有箭头位图按其尺寸, 否则用内置方块尺寸 */
#define POINTER_RECT_W (gHasMouseArrow ? (int)gMouseArrow.w : MOUSE_POINTER_W)
#define POINTER_RECT_H (gHasMouseArrow ? (int)gMouseArrow.h : MOUSE_POINTER_H)

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

void graphic_main(unsigned int magic, unsigned int addr) {
    (void)magic; (void)addr;

    if (vbeSetMode(vbeWidth, vbeHeight, 32) != 0) {
        messageBox("Can't enter graphical mode\n");
        return;
    }

    vbeSetColor(vbeColor(0, 0, 0));
    vbeClearScreen();

    BmpImage wallpaper;
    bool hasWallpaper = (bmpLoad("/system/images/wallpaper.bmp", &wallpaper) == 0);
    if (hasWallpaper)
        vbeDrawBitmap(0, 0, wallpaper.pixels, wallpaper.w, wallpaper.h);

    /* 中文显示验证：用 16x16 字库渲染一行带中英文的文本 */
    vbeSetColor(vbeColor(0, 255, 0));
    vbeDrawStringCJK(20, 20, "VortexOS 你好世界 中文显示");

    /* 加载鼠标箭头位图(仅一次, 供指针绘制复用) */
    gHasMouseArrow = (bmpLoad("/system/images/mousePointer/arrow.bmp", &gMouseArrow) == 0);

    /* 建一块屏幕快照缓冲：直接快照当前 LFB(含壁纸、已绘制的中文文本等所有静态内容)，
     * 用于恢复被指针覆盖的区域。这样指针盖过再移开时能完整还原，不会把内容擦掉。 */
    size_t shadowPix = (size_t)vbeWidth * (size_t)vbeHeight;
    uint32_t* shadow = (uint32_t*)pmmAllocPages((uint32_t)((shadowPix * 4 + 0xFFF) >> 12));
    uint32_t* lfbSnapshot = (uint32_t*)(uintptr_t)gVbeInfo.lfbAddr;
    for (size_t i = 0; i < shadowPix; i++) shadow[i] = lfbSnapshot[i];

    /* 初始化鼠标: 绑定到屏幕尺寸并居中 */
    mouseSetBounds(vbeWidth, vbeHeight);
    mouseSetPosition(vbeWidth / 2, vbeHeight / 2);
    int curX = mouseGetX();
    int curY = mouseGetY();

    /* 需要接收 IRQ12, 打开中断(若之前被关闭) */
    __asm__ volatile ("sti");
    blitShadowToLfb(shadow, vbeWidth, curX, curY, POINTER_RECT_W, POINTER_RECT_H);
    drawMousePointer(curX, curY);

    for (;;) {
        /* 事件驱动: 无鼠标事件时让出 CPU(hlt 等待中断) */
        while (!mouseHasEvent())
            __asm__ volatile ("hlt");

        int nx = mouseGetX();
        int ny = mouseGetY();

        /* 消费事件并清掉 pending 标志(绝对坐标已由中断更新) */
        int moveDx = 0, moveDy = 0;
        mouseGetMotion(&moveDx, &moveDy);
        (void)moveDx; (void)moveDy;

        /* 指针位置变化才重绘: 用快照恢复旧区域, 再画新指针 */
        if (nx != curX || ny != curY) {
            blitShadowToLfb(shadow, vbeWidth, curX, curY, POINTER_RECT_W, POINTER_RECT_H);
            curX = nx;
            curY = ny;
            drawMousePointer(curX, curY);
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