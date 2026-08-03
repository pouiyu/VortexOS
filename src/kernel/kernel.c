#include "idt.h"
#include <drivers/keyboard.h>
#include <drivers/sound.h>
#include <vga.h>
#include <string.h>

#define THEME1 VGA_COLOR(COLOR_WHITE, COLOR_BLACK)
uint8_t theme = THEME1;

typedef enum {
    MENU_MAIN,
    MENU_SETTINGS,
    MENU_CONFIRM
} MenuState;

MenuState currentMenu = MENU_MAIN;
int selectedIndex = 0;
int selectIndex = -1;

// ==================== 菜单项 ====================
char* mainOptions[] = {
    "Settings",
    "Reboot",
    "Shutdown"
};

char* settingsOptions[] = {
    "Sound: ON",
    "Theme: White",
    "Back"
};

char* confirmOptions[] = {
    "Yes",
    "No"
};

// ==================== 菜单大小 ====================
#define MAIN_SIZE    (sizeof(mainOptions) / sizeof(mainOptions[0]))
#define SETTINGS_SIZE (sizeof(settingsOptions) / sizeof(settingsOptions[0]))
#define CONFIRM_SIZE  (sizeof(confirmOptions) / sizeof(confirmOptions[0]))

// ==================== 获取当前菜单 ====================
char** getCurrentOptions(void) {
    switch (currentMenu) {
        case MENU_MAIN:     return mainOptions;
        case MENU_SETTINGS: return settingsOptions;
        case MENU_CONFIRM:  return confirmOptions;
        default:            return mainOptions;
    }
}

int getCurrentSize(void) {
    switch (currentMenu) {
        case MENU_MAIN:     return MAIN_SIZE;
        case MENU_SETTINGS: return SETTINGS_SIZE;
        case MENU_CONFIRM:  return CONFIRM_SIZE;
        default:            return MAIN_SIZE;
    }
}

// ==================== 辅助函数 ====================
static inline void vgaSetColorByte(uint8_t color) {
    uint8_t fg = color & 0x0F;
    uint8_t bg = (color >> 4) & 0x0F;
    vgaSetColor(fg, bg);
}

static inline uint8_t getCursorCol(void) {
    uint8_t row, col;
    vgaGetCursorPos(&row, &col);
    return col;
}

void putOption(const char* text, bool hover) {
    int textLen = strlen(text);
    uint8_t col = getCursorCol();

    if (hover) {
        vgaSetColorByte(vgaInvertColor(theme));
    } else {
        vgaSetColorByte(theme);
    }

    int remaining = VGA_WIDTH;
    if (remaining > 0) {
        for (int i = 0; i < remaining; i++) {
            vgaPutChar(' ');
        }
    }

    vgaSetCursorPos(0, col);
    vgaPutStr(text);
    vgaSetColorByte(theme);
}

void drawTitle(const char* title) {
    vgaSetCursorPos(0, 0);
    vgaSetColorByte(theme);
    vgaPutStr("=== ");
    vgaPutStr(title);
    vgaPutStr(" ===\n\n");
}

void drawOptions(char** options, int size) {
    for (int i = 0; i < size; i++) {
        vgaSetCursorPos(0, 2 + i);  // col=0, row=2+i
        putOption(options[i], (selectedIndex == i));
    }
}

// ==================== 菜单绘制 ====================
void drawMainMenu(void) {
    vgaClear();
    drawTitle("VortexOS");
    drawOptions(mainOptions, MAIN_SIZE);
}

void drawSettingsMenu(void) {
    vgaClear();
    drawTitle("Settings");
    drawOptions(settingsOptions, SETTINGS_SIZE);
}

void drawConfirmMenu(const char* title) {
    vgaClear();
    drawTitle(title);
    drawOptions(confirmOptions, CONFIRM_SIZE);
}

void drawCurrentMenu(void) {
    switch (currentMenu) {
        case MENU_MAIN:     drawMainMenu(); break;
        case MENU_SETTINGS: drawSettingsMenu(); break;
        case MENU_CONFIRM:  drawConfirmMenu("Confirm?"); break;
    }
}

// ==================== 菜单处理 ====================
void handleMainMenuSelect(void) {
    selectedIndex = 0;
    switch (selectIndex) {
        case 0:
            currentMenu = MENU_SETTINGS;
            drawSettingsMenu();
            break;
        case 1:
            currentMenu = MENU_CONFIRM;
            drawConfirmMenu("Reboot?");
            break;
        case 2:
            currentMenu = MENU_CONFIRM;
            drawConfirmMenu("Shutdown?");
            break;
        default:
            drawMainMenu();
            break;
    }
    selectIndex = -1;
}

void handleSettingsMenuSelect(void) {
    if (selectIndex == 2) {  // Back
        currentMenu = MENU_MAIN;
        selectedIndex = 0;
        drawMainMenu();
    }
    selectIndex = -1;
}

void handleConfirmMenuSelect(void) {
    if (selectIndex == 0) {  // Yes
        vgaClear();
        vgaSetCursorPos(0, 0);
        vgaSetColorByte(theme);
        vgaPutStr("Action confirmed!\n");
        while (1) { __asm__ volatile ("hlt"); }
    } else {  // No
        currentMenu = MENU_MAIN;
        selectedIndex = 0;
        drawMainMenu();
    }
    selectIndex = -1;
}

void handleSelect(void) {
    if (selectIndex == -1) return;
    
    switch (currentMenu) {
        case MENU_MAIN:     handleMainMenuSelect(); break;
        case MENU_SETTINGS: handleSettingsMenuSelect(); break;
        case MENU_CONFIRM:  handleConfirmMenuSelect(); break;
    }
}

// ==================== 主函数 ====================
void kernel_main(unsigned int magic, unsigned int addr) {
    (void)magic;
    (void)addr;
    
    vgaInit();
    idtInit();
    keyboardInit();
    vgaSetColorByte(theme);
    vgaClear();
    
    drawMainMenu();
    
    __asm__ volatile ("sti");
    
    while (1) {
        if (keyboardHasChar()) {
            char c = keyboardGetChar();
            int size = getCurrentSize();
            
            if (c == KEY_UP || c == 'w' || c == 'W') {
                selectedIndex--;
                if (selectedIndex < 0) selectedIndex = size - 1;
                drawCurrentMenu();
            } else if (c == KEY_DOWN || c == 's' || c == 'S') {
                selectedIndex++;
                if (selectedIndex >= size) selectedIndex = 0;
                drawCurrentMenu();
            } else if (c == '\r' || c == '\n' || c == ' ') {
                selectIndex = selectedIndex;
                handleSelect();
            }
        }
        __asm__ volatile ("hlt");
    }
}