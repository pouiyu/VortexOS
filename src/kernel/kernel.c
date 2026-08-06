#include "interruption/idt.h"
#include <keyboard.h>
#include <sound.h>
#include <vga.h>
#include <string.h>
#include <kernel.h>
#include <sysinfo/sysinfo.h>
#include <sysinfo/config.h>
#include <shell/shell.h>
#include <shell/commands.h>
#include "device.h"

uint8_t FG = COLOR_WHITE;
uint8_t BG = COLOR_BLACK;
uint8_t theme;

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

#define COLOR_COUNT (sizeof(ColorOptions) / sizeof(ColorOptions[0]))

typedef enum {
    MENU_MAIN,
    MENU_SETTINGS,
    MENU_SETTINGS_ABOUT,
    MENU_SETTINGS_ABOUT_SYSTEM,
    MENU_SETTINGS_ABOUT_DEVICE,
    MENU_SETTINGS_THEME,
    MENU_SETTINGS_THEME_FG,
    MENU_SETTINGS_THEME_BG,
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
    "Reboot",
    "Shutdown"
};

static const char* const settingsOptions[] = {
    "About",
    "Theme",
    "Back"
};

static const char* const aboutOptions[] = {
    "System Info",
    "Device Info",
    "Back"
};

static const char* const themeOptions[] = {
    "Foreground",
    "Background",
    "Back"
};

static const char* const confirmOptions[] = {
    "Yes",
    "No"
};

#define MAIN_SIZE       (sizeof(mainOptions)     / sizeof(mainOptions[0]))
#define SETTINGS_SIZE   (sizeof(settingsOptions) / sizeof(settingsOptions[0]))
#define ABOUT_SIZE      (sizeof(aboutOptions)    / sizeof(aboutOptions[0]))
#define THEME_SIZE      (sizeof(themeOptions)    / sizeof(themeOptions[0]))
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

static void updateTheme(void) {
    theme = VGA_COLOR(FG, BG);
    vgaSetColorByte(theme);
}

static const char* const* getCurrentOptions(void) {
    switch (currentMenu) {
        case MENU_MAIN:               return mainOptions;
        case MENU_SETTINGS:           return settingsOptions;
        case MENU_SETTINGS_ABOUT:     return aboutOptions;
        case MENU_SETTINGS_THEME:     return themeOptions;
        case MENU_SETTINGS_THEME_FG:
        case MENU_SETTINGS_THEME_BG:  return ColorOptions;
        case MENU_CONFIRM_REBOOT:
        case MENU_CONFIRM_SHUTDOWN:   return confirmOptions;
        default:                      return NULL;
    }
}

static int getCurrentSize(void) {
    switch (currentMenu) {
        case MENU_MAIN:               return MAIN_SIZE;
        case MENU_SETTINGS:           return SETTINGS_SIZE;
        case MENU_SETTINGS_ABOUT:     return ABOUT_SIZE;
        case MENU_SETTINGS_THEME:     return THEME_SIZE;
        case MENU_SETTINGS_THEME_FG:
        case MENU_SETTINGS_THEME_BG:  return COLOR_COUNT;
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
    drawOptions(ColorOptions, COLOR_COUNT);
}

static void drawThemeOptionsMenu(void) {
    vgaClear();
    drawTitle("Theme");
    drawOptions(themeOptions, THEME_SIZE);
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
        case MENU_SETTINGS_THEME_FG:  drawThemeMenu("Foreground Color"); break;
        case MENU_SETTINGS_THEME_BG:  drawThemeMenu("Background Color"); break;
        case MENU_CONFIRM_REBOOT:     drawConfirmMenu("Reboot?");  break;
        case MENU_CONFIRM_SHUTDOWN:   drawConfirmMenu("Shutdown?"); break;
    }
}

void messageBox(const char* msg) {
    vgaPutStr(msg);
    vgaPutStr("\n\nPress any key to return...");
    while (!keyboardHasChar()) __asm__ volatile ("hlt");
    keyboardGetChar();
}

static void handleMainMenuSelect(void) {
    switch (selectIndex) {
        case 0: pushMenu(MENU_SETTINGS, 0);         break;
        case 1: runShell();                        break;  // Shell
        case 2: pushMenu(MENU_CONFIRM_REBOOT, 0);    break;
        case 3: pushMenu(MENU_CONFIRM_SHUTDOWN, 0);  break;
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
        case 2:  // Back
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
            pushMenu(MENU_SETTINGS_THEME_FG, FG);
            drawCurrentMenu();
            return;
        case 1:
            pushMenu(MENU_SETTINGS_THEME_BG, BG);
            drawCurrentMenu();
            return;
        case 2:
            popMenu();
            drawCurrentMenu();
            return;
    }
    selectIndex = -1;
}

static void handleThemeFGSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < COLOR_COUNT) {
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

static void handleThemeBGSelect(void) {
    if (selectIndex >= 0 && (unsigned int)selectIndex < COLOR_COUNT) {
        uint8_t newBG = (uint8_t)selectIndex;
        if (newBG == FG) {
            vgaClear();
            messageBox("Background cannot be the\nsame as foreground!");
        } else {
            BG = newBG;
            updateTheme();
        }
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
        case MENU_SETTINGS_THEME_FG:  handleThemeFGSelect();     break;
        case MENU_SETTINGS_THEME_BG:  handleThemeBGSelect();     break;
        case MENU_CONFIRM_REBOOT:     handleConfirmReboot();     break;
        case MENU_CONFIRM_SHUTDOWN:   handleConfirmShutdown();   break;
    }
}

void kernel_main(unsigned int magic, unsigned int addr) {
    (void)magic;
    (void)addr;

    theme = VGA_COLOR(FG, BG);

    vgaInit();
    idtInit();
    keyboardInit();
    vgaSetColorByte(theme);
    vgaClear();
    drawMainMenu();

    __asm__ volatile ("sti");

    for (;;) {
        if (keyboardHasChar()) {
            char c = keyboardGetChar();
            int size = getCurrentSize();

            if (c == KEY_UP || c == 'w' || c == 'W') {
                if (--selectedIndex < 0)
                    selectedIndex = size - 1;
                drawCurrentMenu();
            } else if (c == KEY_DOWN || c == 's' || c == 'S') {
                if (++selectedIndex >= size)
                    selectedIndex = 0;
                drawCurrentMenu();
            } else if (c == '\r' || c == '\n' || c == ' ') {
                selectIndex = selectedIndex;
                handleSelect();
            }
        }
        __asm__ volatile ("hlt");
    }
}