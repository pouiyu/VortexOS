#include <keyboard.h>
#include <vga.h>
#include <io.h>

unsigned char keyboardScancode = 0;
static bool extendedCode = false;
static bool releaseCode = false;

static const char scancodeToAscii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancodeToAsciiShift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static bool shiftPressed = false;
static bool capsLockOn = false;

// 处理扩展扫描码（方向键、功能键等）
static char handleExtendedScancode(unsigned char scancode) {
    switch (scancode) {
        case 0x48: return 0x80;  // 上箭头
        case 0x50: return 0x81;  // 下箭头
        case 0x4B: return 0x82;  // 左箭头
        case 0x4D: return 0x83;  // 右箭头
        case 0x47: return 0x84;  // Home
        case 0x4F: return 0x85;  // End
        case 0x49: return 0x86;  // Page Up
        case 0x51: return 0x87;  // Page Down
        case 0x53: return 0x88;  // Delete
        case 0x52: return 0x89;  // Insert
        case 0x1C: return 0x8A;  // Keypad Enter
        case 0x35: return 0x8B;  // Keypad /
        default: return 0;
    }
}

void keyboardIRQHandler(void) {
    unsigned char scancode = inb(0x60);
    
    // 处理扩展码前缀 E0
    if (scancode == 0xE0) {
        extendedCode = true;
        outb(0x20, 0x20);
        return;
    }
    
    // 处理释放码前缀 F0
    if (scancode == 0xF0) {
        releaseCode = true;
        outb(0x20, 0x20);
        return;
    }
    
    // 如果是扩展码（方向键等）
    if (extendedCode) {
        if (!releaseCode) {
            keyboardScancode = handleExtendedScancode(scancode);
        }
        extendedCode = false;
        releaseCode = false;
        outb(0x20, 0x20);
        return;
    }
    
    // 普通扫描码
    if (scancode < 0x80) {
        keyboardScancode = scancode;
        switch (scancode) {
            case 0x2A:
            case 0x36:
                shiftPressed = true;
                break;
            case 0xAA:
                capsLockOn = !capsLockOn;
                break;
        }
    } else {
        // 释放码
        keyboardScancode = 0;
        switch (scancode - 0x80) {
            case 0x2A:
            case 0x36:
                shiftPressed = false;
                break;
        }
    }
    
    outb(0x20, 0x20);
}

void keyboardInit(void) {
    keyboardScancode = 0;
    shiftPressed = false;
    capsLockOn = false;
    extendedCode = false;
    releaseCode = false;

}

char keyboardGetChar(void) {
    if (keyboardScancode) {
        unsigned char scancode = keyboardScancode;
        keyboardScancode = 0;

        // 扩展键（方向键等）
        if (scancode >= 0x80) {
            return scancode;
        }

        // 先处理特殊键，直接返回扫描码本身
        switch (scancode) {
            case 0x1C: return '\n';   // 回车
            case 0x0E: return '\b';   // 退格
            case 0x0F: return '\t';   // Tab
            case 0x01: return KEY_ESC;// Esc
            case 0x39: return ' ';    // 空格（表中已有但确保）
        }

        // 普通可打印字符
        bool upper = shiftPressed ^ capsLockOn;
        char c = upper ? scancodeToAsciiShift[scancode] : scancodeToAscii[scancode];

        if (c != 0) {
            return c;
        }
    }
    return 0;
}

bool keyboardHasChar(void) {
    return keyboardScancode != 0;
}

void keyboardSetHandler(void (*handler)(KeyEvent*)) {
    (void)handler;
}

void keyboardSetScanCodeSet(uint8_t set) {
    (void)set;
}