#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <io.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

#define VGA_CTRL_REG 0x3D4
#define VGA_DATA_REG 0x3D5
#define VGA_CURSOR_HI 0x0E
#define VGA_CURSOR_LO 0x0F
#define VGA_CURSOR_START 0x0A
#define VGA_CURSOR_END   0x0B

enum VgaColor {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_YELLOW = 14,
    COLOR_WHITE = 15
};

static inline uint8_t vgaEntryColor(uint8_t fg, uint8_t bg) {
    return fg | bg << 4;
}

static inline uint16_t vgaEntry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static inline uint8_t vgaInvertColor(uint8_t color) {
    uint8_t fg = color & 0x0F;
    uint8_t bg = (color >> 4) & 0x0F;
    return vgaEntryColor(bg, fg);
}

static inline uint8_t vgaGetFg(uint8_t color) {
    return color & 0x0F;
}

static inline uint8_t vgaGetBg(uint8_t color) {
    return (color >> 4) & 0x0F;
}

#define VGA_COLOR(fg, bg) ((fg) | ((bg) << 4))

uint8_t vgaGetCursorRow(void);
uint8_t vgaGetCursorCol(void);

void vgaInit(void);
void vgaClear(void);
void vgaClearColor(void);
void vgaClearFgColor(void);
void vgaClearBgColor(void);
void vgaClearChar(char c);
void vgaSetCursorPos(uint8_t row, uint8_t col);
void vgaGetCursorPos(uint8_t* row, uint8_t* col);
void vgaSetCursorStyle(uint8_t start, uint8_t end);
void vgaEnableCursor(void);
void vgaDisableCursor(void);
void vgaPutChar(char c);
void vgaPutCharColor(char c, uint8_t color);
void vgaPutStr(const char* str);
void vgaPutStrColor(const char* str, uint8_t color);
void vgaScroll(uint8_t lines);
void vgaSetColor(uint8_t foreground, uint8_t background);
uint8_t vgaGetColor(void);
void putDecimal(uint32_t value);
void vgaPutHex8(uint8_t value);
void vgaPutHex16(uint16_t value);
void vgaPutHex32(uint32_t value);
void vgaPutColor(void);
void vgaFillLineColor(void);
void vgaPutColorRange(uint8_t row, uint8_t startCol, uint8_t endCol);

#endif