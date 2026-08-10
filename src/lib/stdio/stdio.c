#include <vga.h>
#include <stdio.h>
#include <io.h>

static uint16_t* videoMemory = (uint16_t*) VGA_MEMORY;
static uint8_t cursorRow = 0;
static uint8_t cursorCol = 0;
static uint8_t currentColor = 0;

void vgaInit(void) {
    currentColor = vgaEntryColor(COLOR_LIGHT_GREY, COLOR_BLACK);
    vgaClear();
}

uint8_t vgaGetCursorRow(void) {
    return cursorRow;
}

uint8_t vgaGetCursorCol(void) {
    return cursorCol;
}

void vgaSetCursorPos(uint8_t row, uint8_t col) {
    cursorRow = row;
    cursorCol = col;
    uint16_t pos = row * VGA_WIDTH + col;
    outb(VGA_CTRL_REG, VGA_CURSOR_HI);
    outb(VGA_DATA_REG, (pos >> 8) & 0xFF);
    outb(VGA_CTRL_REG, VGA_CURSOR_LO);
    outb(VGA_DATA_REG, pos & 0xFF);
}

void vgaGetCursorPos(uint8_t* row, uint8_t* col) {
    if (row) *row = cursorRow;
    if (col) *col = cursorCol;
}

void vgaDisableCursor(void) {
    outb(0x3D4, 0x0A);  // 光标起始寄存器
    outb(0x3D5, 0x20);  // 设置第5位为1禁用光标
}

void vgaEnableCursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);  // 恢复默认值
}

void vgaPutColor(void) {
    size_t index = cursorRow * VGA_WIDTH + cursorCol;
    uint16_t entry = videoMemory[index];
    uint8_t ch = entry & 0xFF;
    videoMemory[index] = vgaEntry(ch, currentColor);
}

void vgaPutColorRow(uint8_t row) {
    for (int col = 0; col < VGA_WIDTH; col++) {
        size_t index = row * VGA_WIDTH + col;
        uint16_t entry = videoMemory[index];
        uint8_t ch = entry & 0xFF;
        videoMemory[index] = vgaEntry(ch, currentColor);
    }
}

void vgaPutColorRange(uint8_t row, uint8_t startCol, uint8_t endCol) {
    for (int col = startCol; col < endCol; col++) {
        size_t index = row * VGA_WIDTH + col;
        uint16_t entry = videoMemory[index];
        uint8_t ch = entry & 0xFF;
        videoMemory[index] = vgaEntry(ch, currentColor);
    }
}

void vgaClear(void) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(' ', currentColor);
        }
    }
    cursorRow = 0;
    cursorCol = 0;
    vgaSetCursorPos(0, 0);
}

void vgaPutChar(char c) {
    vgaPutCharColor(c, currentColor);
}

void vgaPutCharColor(char c, uint8_t color) {
    if (c == '\n') {
        cursorCol = 0;
        cursorRow++;
        if (cursorRow >= VGA_HEIGHT) {
            vgaScroll(1);
            cursorRow = VGA_HEIGHT - 1;
        }
        vgaSetCursorPos(cursorRow, cursorCol);
        return;
    }

    if (c == '\t') {
        do {
            vgaPutCharColor(' ', color);
        } while (cursorCol % 4 != 0);
        return;
    }

    if (c == '\r') {
        cursorCol = 0;
        vgaSetCursorPos(cursorRow, cursorCol);
        return;
    }

    if (c == '\b') {
        if (cursorCol > 0) {
            cursorCol--;
            size_t index = cursorRow * VGA_WIDTH + cursorCol;
            videoMemory[index] = vgaEntry(' ', color);
            vgaSetCursorPos(cursorRow, cursorCol);
        }
        return;
    }

    size_t index = cursorRow * VGA_WIDTH + cursorCol;
    videoMemory[index] = vgaEntry(c, color);

    cursorCol++;
    if (cursorCol >= VGA_WIDTH) {
        cursorCol = 0;
        cursorRow++;
        if (cursorRow >= VGA_HEIGHT) {
            vgaScroll(1);
            cursorRow = VGA_HEIGHT - 1;
        }
    }
    vgaSetCursorPos(cursorRow, cursorCol);
}

void vgaPutStr(const char* str) {
    vgaPutStrColor(str, currentColor);
}

void vgaPutStrColor(const char* str, uint8_t color) {
    while (*str) {
        vgaPutCharColor(*str++, color);
    }
}

void vgaScroll(uint8_t lines) {
    for (int row = lines; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t src = row * VGA_WIDTH + col;
            size_t dst = (row - lines) * VGA_WIDTH + col;
            videoMemory[dst] = videoMemory[src];
        }
    }

    for (int row = VGA_HEIGHT - lines; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(' ', currentColor);
        }
    }
}

void vgaSetColor(uint8_t foreground, uint8_t background) {
    currentColor = vgaEntryColor(foreground, background);
}

uint8_t vgaGetColor(void) {
    return currentColor;
}

void vgaPutHex8(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    vgaPutChar(hex[(value >> 4) & 0x0F]);
    vgaPutChar(hex[value & 0x0F]);
}

void vgaPutHex16(uint16_t value) {
    vgaPutHex8((value >> 8) & 0xFF);
    vgaPutHex8(value & 0xFF);
}

void vgaPutHex32(uint32_t value) {
    vgaPutHex16((value >> 16) & 0xFFFF);
    vgaPutHex16(value & 0xFFFF);
}