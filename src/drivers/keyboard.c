// keyboard.c
// PS/2 键盘驱动：同时支持扫描码集合 1(AT, QEMU/VMware 常见)与集合 2(PC 通电默认,
// 部分真机 BIOS/GRUB 不切换时保持 set2)。通过 0xF0 释放前缀自动检测集合,
// 按键放入环形缓冲, 供 Shell/安装向导轮询读取。
#include <keyboard.h>
#include <stdio/vga.h>
#include <io.h>
#include <serial.h>

#define KBD_RING_SIZE 16

/* 环形按键缓冲(IRQ 写 / 主循环读) */
static unsigned char kbdRing[KBD_RING_SIZE];
static volatile int kbdHead = 0;   /* 下一个读出位置 */
static volatile int kbdTail = 0;   /* 下一个写入位置 */

/* 当前扫描码集合: 0=未知, 1=set1, 2=set2 */
static int kbdSet = 0;
static bool extendedCode = false;
static bool releaseCode = false;

static bool shiftPressed = false;
static bool capsLockOn = false;
static bool ctrlPressed = false;

/* 集合未知时暂存的 make 码: 等释放码确定集合后再解码, 保证第一次按键也正确 */
static bool pendingMake = false;
static unsigned char pendingCode = 0;

/* ============ set 1 解码表 ============ */
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

/* ============ set 2 解码表 (make 码 -> ASCII, 未列出项为 0) ============ */
static const char sc2Ascii[128] = {
    ['\x0E'] = '`',
    ['\x16'] = '1', ['\x1E'] = '2', ['\x26'] = '3', ['\x25'] = '4',
    ['\x2E'] = '5', ['\x36'] = '6', ['\x3D'] = '7', ['\x3E'] = '8',
    ['\x46'] = '9', ['\x45'] = '0',
    ['\x4E'] = '-', ['\x55'] = '=',
    ['\x15'] = 'q', ['\x1D'] = 'w', ['\x24'] = 'e', ['\x2D'] = 'r',
    ['\x2C'] = 't', ['\x35'] = 'y', ['\x3C'] = 'u', ['\x43'] = 'i',
    ['\x44'] = 'o', ['\x4D'] = 'p', ['\x54'] = '[', ['\x5B'] = ']',
    ['\x5D'] = '\\',
    ['\x1C'] = 'a', ['\x1B'] = 's', ['\x23'] = 'd', ['\x2B'] = 'f',
    ['\x34'] = 'g', ['\x33'] = 'h', ['\x3B'] = 'j', ['\x42'] = 'k',
    ['\x4B'] = 'l', ['\x4C'] = ';', ['\x52'] = '\'',
    ['\x1A'] = 'z', ['\x22'] = 'x', ['\x21'] = 'c', ['\x2A'] = 'v',
    ['\x32'] = 'b', ['\x31'] = 'n', ['\x3A'] = 'm',
    ['\x41'] = ',', ['\x49'] = '.', ['\x4A'] = '/',
    ['\x29'] = ' '
};

/* set 2 上档字符 */
static const char sc2AsciiShift[128] = {
    ['\x0E'] = '~',
    ['\x16'] = '!', ['\x1E'] = '@', ['\x26'] = '#', ['\x25'] = '$',
    ['\x2E'] = '%', ['\x36'] = '^', ['\x3D'] = '&', ['\x3E'] = '*',
    ['\x46'] = '(', ['\x45'] = ')',
    ['\x4E'] = '_', ['\x55'] = '+',
    ['\x15'] = 'Q', ['\x1D'] = 'W', ['\x24'] = 'E', ['\x2D'] = 'R',
    ['\x2C'] = 'T', ['\x35'] = 'Y', ['\x3C'] = 'U', ['\x43'] = 'I',
    ['\x44'] = 'O', ['\x4D'] = 'P', ['\x54'] = '{', ['\x5B'] = '}',
    ['\x5D'] = '|',
    ['\x1C'] = 'A', ['\x1B'] = 'S', ['\x23'] = 'D', ['\x2B'] = 'F',
    ['\x34'] = 'G', ['\x33'] = 'H', ['\x3B'] = 'J', ['\x42'] = 'K',
    ['\x4B'] = 'L', ['\x4C'] = ':', ['\x52'] = '"',
    ['\x1A'] = 'Z', ['\x22'] = 'X', ['\x21'] = 'C', ['\x2A'] = 'V',
    ['\x32'] = 'B', ['\x31'] = 'N', ['\x3A'] = 'M',
    ['\x41'] = '<', ['\x49'] = '>', ['\x4A'] = '?',
    ['\x29'] = ' '
};

/* 扩展键(E0 前缀)扫描码, set1 与 set2 各自映射到统一码 */
static char handleExtendedScancode(unsigned char scancode) {
    if (kbdSet == 2) {
        switch (scancode) {
            case 0x75: return 0x80;  /* Up */
            case 0x72: return 0x81;  /* Down */
            case 0x6B: return 0x82;  /* Left */
            case 0x74: return 0x83;  /* Right */
            case 0x6C: return 0x84;  /* Home */
            case 0x69: return 0x85;  /* End */
            case 0x7D: return 0x86;  /* Page Up */
            case 0x7A: return 0x87;  /* Page Down */
            case 0x71: return 0x88;  /* Delete */
            case 0x70: return 0x89;  /* Insert */
            case 0x5A: return 0x8A;  /* Keypad Enter */
            case 0x4A: return 0x8B;  /* Keypad / */
            default: return 0;
        }
    }
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
    keyboardProcessScancode(scancode);
    outb(0x20, 0x20);
}

/* 把扫描码写入环形缓冲(满则丢弃新键) */
static void kbdPush(unsigned char c) {
    int next = (kbdTail + 1) % KBD_RING_SIZE;
    if (next != kbdHead) {
        kbdRing[kbdTail] = c;
        kbdTail = next;
    }
}

/* 按当前集合把 make 码解码为字符 */
static char decodeKey(unsigned char sc) {
    if (kbdSet == 2) {
        switch (sc) {
            case 0x5A: return '\n';
            case 0x66: return '\b';
            case 0x0D: return '\t';
            case 0x76: return KEY_ESC;
            case 0x29: return ' ';
        }
        bool upper = shiftPressed ^ capsLockOn;
        char c = upper ? sc2AsciiShift[sc] : sc2Ascii[sc];
        return c;
    }
    switch (sc) {
        case 0x1C: return '\n';   // 回车
        case 0x0E: return '\b';   // 退格
        case 0x0F: return '\t';   // Tab
        case 0x01: return KEY_ESC;
        case 0x39: return ' ';    // 空格
    }
    bool upper = shiftPressed ^ capsLockOn;
    return upper ? scancodeToAsciiShift[sc] : scancodeToAscii[sc];
}

/* 处理一个 PS/2 扫描码（含 E0/F0 前缀状态机）。供 PS/2 IRQ 与 USB HID 注入共用。 */
void keyboardProcessScancode(unsigned char scancode) {
    /* set 2 释放前缀：0xF0 一旦出现即确认键盘在 set 2 */
    if (scancode == 0xF0) {
        if (kbdSet == 0) {
            kbdSet = 2;
            serialPutStr("[KBD] scancode set 2 detected\n");
        }
        releaseCode = true;
        return;
    }

    /* 扩展码前缀 */
    if (scancode == 0xE0) {
        extendedCode = true;
        return;
    }

    if (extendedCode) {
        if (scancode == 0x1D) {           // 右 Ctrl (E0 1D)
            ctrlPressed = !releaseCode;
        } else if (!releaseCode) {
            unsigned char v = (unsigned char)handleExtendedScancode(scancode);
            if (v) {
                // Ctrl+方向键 → 滚动屏幕
                if (ctrlPressed && v == 0x80) v = (unsigned char)KEY_SCROLL_UP;
                else if (ctrlPressed && v == 0x81) v = (unsigned char)KEY_SCROLL_DOWN;
                kbdPush(v);
            }
        }
        extendedCode = false;
        releaseCode = false;
        return;
    }

    /* ---- 集合已确定：即时处理 ---- */
    if (kbdSet == 2) {
        if (releaseCode) {
            /* set 2 释放码 */
            switch (scancode) {
                case 0x12: case 0x59: shiftPressed = false; break;
                case 0x14: ctrlPressed = false; break;
            }
            /* 集合刚确定时补解缓存的 make */
            if (pendingMake) { kbdPush(pendingCode); pendingMake = false; }
            releaseCode = false;
            return;
        }
        switch (scancode) {
            case 0x12: case 0x59: shiftPressed = true; return;
            case 0x14: ctrlPressed = true; return;
            case 0x58: capsLockOn = !capsLockOn; return;
        }
        kbdPush(scancode);
        return;
    }

    if (kbdSet == 1) {
        if (scancode >= 0x80) {
            /* set 1 释放码 */
            unsigned char mk = scancode & 0x7F;
            if (mk == 0x2A || mk == 0x36) shiftPressed = false;
            else if (mk == 0x1D) ctrlPressed = false;
            if (pendingMake) { kbdPush(pendingCode); pendingMake = false; }
            return;
        }
        switch (scancode) {
            case 0x2A: case 0x36: shiftPressed = true; return;
            case 0x3A: capsLockOn = !capsLockOn; return;
            case 0x1D: ctrlPressed = true; return;
        }
        kbdPush(scancode);
        return;
    }

    /* ---- kbdSet == 0：集合未知 ----
     * 无 0xF0 前缀的高位字节只能是 set 1 的释放码，据此确定集合并补解缓存。 */
    if (scancode >= 0x80) {
        kbdSet = 1;
        unsigned char mk = scancode & 0x7F;
        if (mk == 0x2A || mk == 0x36) shiftPressed = false;
        else if (mk == 0x1D) ctrlPressed = false;
        if (pendingMake) { kbdPush(pendingCode); pendingMake = false; }
        return;
    }
    /* 普通字节：先缓存为 make，等释放码确定集合后再解码，
     * 保证真机 set2 / QEMU set1 的第一次按键都正确响应。 */
    pendingMake = true;
    pendingCode = scancode;
}

void keyboardInit(void) {
    kbdHead = 0;
    kbdTail = 0;
    kbdSet = 0;
    shiftPressed = false;
    capsLockOn = false;
    ctrlPressed = false;
    extendedCode = false;
    releaseCode = false;
    pendingMake = false;
    pendingCode = 0;
}

char keyboardGetChar(void) {
    if (kbdHead == kbdTail) return 0;
    unsigned char sc = kbdRing[kbdHead];
    kbdHead = (kbdHead + 1) % KBD_RING_SIZE;

    /* 扩展键（方向键等）直接返回统一码 */
    if (sc >= 0x80) return (char)sc;
    return decodeKey(sc);
}

bool keyboardHasChar(void) {
    return kbdHead != kbdTail;
}

void keyboardSetHandler(void (*handler)(KeyEvent*)) {
    (void)handler;
}

void keyboardSetScanCodeSet(uint8_t set) {
    (void)set;
}
