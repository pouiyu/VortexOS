#include <stdio/vga.h>
#include "stdio.h"
#include <io.h>
#include <string/string.h>
#include <stdio/vbe.h>

// VGA

static uint16_t* videoMemory = (uint16_t*) VGA_MEMORY;
/* 帧缓冲模式下的 RAM 文本模型：GRUB 图形模式下 VGA 文本窗口(0xB8000)处于
 * 平面映射状态，经 CPU 读写会把单元格错位/残留成白色大字(0xFFFF 块)。
 * 进入帧缓冲输出时把文本模型切换到这块纯 RAM，彻底绕开 0xB8000 窗口。 */
static uint16_t sTextModel[VGA_HEIGHT][VGA_WIDTH];
static uint8_t cursorRow = 0;
static uint8_t cursorCol = 0;
static uint8_t currentColor = 0;
static uint8_t cursorStyleStart = 0;   // 最近设置的光标样式，供恢复
static uint8_t cursorStyleEnd = 15;

/* ===== 帧缓冲文本后端 =====
 * 引导器(GRUB)提供的帧缓冲模式下，VGA 文本平面(0xB8000)不再接显示屏。
 * 这里把 0xB8000 当作 80x25 文本模型(现有逻辑照常读写)，sFbOut 时把差异
 * 逐格画到帧缓冲：sPrev 记录上次已推送的镜像，只重画变化的格子，光标单独画方块。 */
static bool     sFbOut = false;
static uint16_t sPrev[VGA_HEIGHT][VGA_WIDTH];
static bool     sCursorVisible = false;
/* 批处理计数：vgaPutStrColor 循环内抑制逐字符刷新(帧缓冲模式下逐字整屏 diff
 * + 逐像素写 VRAM 会导致"打字机"式逐字输出)，循环结束统一刷新一次。
 * 单独的 vgaPutChar 调用(sBatch==0)仍即时刷新，避免 date/异常等纯单字符调用不刷屏。 */
static int      sBatch = 0;

/* 上次刷新的网格居中偏移：变化时需整帧清除+全量重画，避免换分辨率后残留鬼影 */
static int      sLastCenterX = -1;
static int      sLastCenterY = -1;

static const uint32_t sVgaPalette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

void vgaSetFramebufferOutput(bool enable) {
    if (sFbOut == enable) return;
    sFbOut = enable;
    if (enable) {
        /* 切到 RAM 文本模型：把 0xB8000 现有内容搬进镜像(保留此前文本状态)，
         * 并清洗平面错乱产生的白色残留单元(0xFFFF=白底白字，画面不可见内容)。
         * 之后所有读写都走 sTextModel，不再触碰有平面映射问题的 VGA 窗口。 */
        memcpy(sTextModel, (uint16_t*)VGA_MEMORY, sizeof(sTextModel));
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++)
            if (sTextModel[0][i] == 0xFFFF)
                sTextModel[0][i] = vgaEntry(' ', currentColor);
        videoMemory = (uint16_t*)sTextModel;
        /* 首次推送把整个现有画面当“变化”画出(含此前积压的启动文本) */
        for (int r = 0; r < VGA_HEIGHT; r++)
            for (int c = 0; c < VGA_WIDTH; c++)
                sPrev[r][c] = vgaEntry(' ', currentColor);
        sCursorVisible = false;
    } else {
        /* 回到硬件文本模式：把 RAM 模型写回 0xB8000 文本平面 */
        memcpy((uint16_t*)VGA_MEMORY, sTextModel, sizeof(sTextModel));
        videoMemory = (uint16_t*)VGA_MEMORY;
    }
    vgaOutputFlush();
}

void vgaOutputFlush(void) {
    if (!sFbOut) return;

    int x0 = (vbeWidth  - VGA_WIDTH  * VBE_FONT_W) / 2; if (x0 < 0) x0 = 0;
    int y0 = (vbeHeight - VGA_HEIGHT * VBE_FONT_H) / 2; if (y0 < 0) y0 = 0;

    /* 分辨率/居中偏移一旦变化，旧网格落笔的像素与新网格错位，其位置不再被任何
     * 格子覆盖而残留在屏上(鬼影/背景文字)。此时强制全量重画，并先把整帧清除
     * 成背景色，确保旧偏移处的文字被抹掉。 */
    if (x0 != sLastCenterX || y0 != sLastCenterY) {
        sLastCenterX = x0;
        sLastCenterY = y0;
        for (int r = 0; r < VGA_HEIGHT; r++)
            for (int c = 0; c < VGA_WIDTH; c++)
                sPrev[r][c] = 0xFFFF;                     /* 使其必然 != e, 全量重画 */
        uint32_t clearBg = sVgaPalette[(currentColor >> 4) & 0x0F];
        vbeFbFillRect(0, 0, vbeWidth, vbeHeight, clearBg); /* 抹平残留像素 */
    }

    for (int r = 0; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            uint16_t e = videoMemory[r * VGA_WIDTH + c];
            if (sPrev[r][c] == e) continue;
            sPrev[r][c] = e;
            int px = x0 + c * VBE_FONT_W;
            int py = y0 + r * VBE_FONT_H;
            uint32_t fg = sVgaPalette[(e >> 8) & 0x0F];
            uint32_t bg = sVgaPalette[(e >> 12) & 0x0F];
            vbeFbFillRect(px, py, VBE_FONT_W, VBE_FONT_H, bg);
            vbeFbTextCell(px, py, (uint8_t)(e & 0xFF), fg, bg);
        }
    }

    if (sCursorVisible) {
        int px = x0 + cursorCol * VBE_FONT_W;
        int py = y0 + cursorRow * VBE_FONT_H;
        uint16_t e = videoMemory[cursorRow * VGA_WIDTH + cursorCol];
        vbeFbFillRect(px, py, VBE_FONT_W, VBE_FONT_H, sVgaPalette[(e >> 8) & 0x0F]);
    }
}

/* ===== 屏幕滚动回看 =====
 * scrollCount  : 本屏会话内已「完成」的行数(光标离开该行即视为完成)
 * scrollRows   : 已完成行的环状缓冲，保存完整 80 列单元格(字符+颜色)
 * liveScreen   : 进入滚动态时的实时画面快照，用于恢复实时视图
 * scrollViewOffset/LiveTop : 滚动视图顶行相对实时顶行的偏移 */
#define SCROLL_ROWS 512
static uint16_t scrollRows[SCROLL_ROWS][VGA_WIDTH];
static uint16_t liveScreen[VGA_HEIGHT][VGA_WIDTH];
static int scrollCount = 0;
static int scrollViewOffset = 0;   // 0 = 实时视图
static int scrollLiveTop = 0;

// 把指定行写入回看缓冲(光标即将离开该行时调用)
static void captureRow(uint8_t row) {
    for (int c = 0; c < VGA_WIDTH; c++)
        scrollRows[scrollCount % SCROLL_ROWS][c] = videoMemory[row * VGA_WIDTH + c];
    scrollCount++;
}

// 渲染滚动视图(顶行 = scrollLiveTop - scrollViewOffset)
static void renderScrolledView(void) {
    int top = scrollLiveTop - scrollViewOffset;
    for (int r = 0; r < VGA_HEIGHT; r++) {
        int line = top + r;
        for (int c = 0; c < VGA_WIDTH; c++) {
            size_t idx = r * VGA_WIDTH + c;
            if (line >= 0 && line < scrollCount)
                videoMemory[idx] = scrollRows[line % SCROLL_ROWS][c];
            else
                videoMemory[idx] = vgaEntry(' ', currentColor);
        }
    }
    vgaOutputFlush();
}

// 返回占用显存的行快照并恢复实时视图
static void restoreLiveView(void) {
    memcpy(videoMemory, liveScreen, VGA_HEIGHT * VGA_WIDTH * sizeof(uint16_t));
    vgaSetCursorPos(cursorRow, cursorCol);
    vgaOutputFlush();
}

void vgaScrollView(int delta) {
    if (delta == 0) return;

    if (scrollViewOffset == 0) {
        // 实时态：向下没有可滚方向；需有回看内容才能向上滚
        if (delta < 0) return;
        int top = scrollCount - (int)cursorRow;
        if (top < 0) top = 0;
        if (top < 1) return;              // 没有可回看的历史行
        scrollLiveTop = top;
        // 进入滚动态：保存实时快照，并记录实时顶行
        memcpy(liveScreen, videoMemory, VGA_HEIGHT * VGA_WIDTH * sizeof(uint16_t));
        vgaDisableCursor();
        scrollViewOffset = 1;
        renderScrolledView();
        return;
    }

    int newOff = scrollViewOffset + delta;
    if (newOff < 1) {
        // 回到实时视图
        vgaScrollViewReset();
        return;
    }
    if (newOff > scrollLiveTop) newOff = scrollLiveTop;
    scrollViewOffset = newOff;
    renderScrolledView();
}

void vgaScrollViewReset(void) {
    if (scrollViewOffset == 0) return;
    scrollViewOffset = 0;
    restoreLiveView();
    vgaEnableCursor();
}

int vgaScrollViewActive(void) {
    return scrollViewOffset != 0;
}

/* 把 256×16 的 8x16 点阵字库上传到 VGA 字模平面(plane 2)。
 * 时序见 OSDev "VGA Fonts / Get from VGA RAM directly"（读写同构，方向互换）：
 * 1) GC 模式寄存器 关奇偶寻址 + GC Misc 关 chain-4 并把窗口映射到 0xA0000；
 * 2) 序发生器 映射掩码 = 0x04（只写 plane 2 字模），内存模式关奇偶；
 * 3) 每字符占 32 字节槽位：前 16 字节为 8x16 扫描线，后 16 字节为 8x32 预留(置零)；
 * 4) 写毕恢复各寄存器(掩码回 plane0/1 字符+属性、重开奇偶与 chain-4)。
 * 必须在文本模式、进入 VBE 图形模式之前调用。 */
void vgaLoadFont(const uint8_t* glyphs) {
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* GC 模式: 写0读0, 关奇偶 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);   /* GC Misc: 关 chain-4, 映射 A0000 */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* 序发生器: 映射掩码 = plane 2(字模) */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);   /* 序发生器: 内存模式, 关奇偶 */

    uint8_t* vga = (uint8_t*)VGA_FONT_MEMORY;
    for (uint16_t i = 0; i < 256; i++) {
        uint8_t* slot = &vga[(size_t)i * 32];
        for (uint8_t j = 0; j < 32; j++) slot[j] = 0;             /* 清 32 字节槽 */
        for (uint8_t j = 0; j < 16; j++) slot[j] = glyphs[(size_t)i * 16 + j];
    }

    outb(0x3C4, 0x02); outb(0x3C5, 0x03);   /* 掩码回 plane0,1(字符+属性) */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);   /* 内存模式: 重开奇偶 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);   /* GC 模式: 写0读0, 开奇偶 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);   /* GC Misc: 重开 chain-4, 映射 A0000 */
}

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

void vgaSetCursorStyle(uint8_t start, uint8_t end) {
    cursorStyleStart = start;
    cursorStyleEnd = end;
    outb(VGA_CTRL_REG, VGA_CURSOR_START);
    outb(VGA_DATA_REG, (inb(VGA_DATA_REG) & 0xC0) | (start & 0x1F));
    outb(VGA_CTRL_REG, VGA_CURSOR_END);
    outb(VGA_DATA_REG, (inb(VGA_DATA_REG) & 0xE0) | (end & 0x1F));
}

void vgaDisableCursor(void) {
    if (sFbOut) sCursorVisible = false;
    outb(VGA_CTRL_REG, VGA_CURSOR_START);
    outb(VGA_DATA_REG, 0x20);
}

void vgaEnableCursor(void) {
    // 禁用(0x20)会覆写起始扫描线寄存器，
    // 因此在启用时恢复上次设置的光标样式，避免样式被改动
    if (sFbOut) sCursorVisible = true;
    vgaSetCursorStyle(cursorStyleStart, cursorStyleEnd);
}

void vgaPutColor(void) {
    size_t index = cursorRow * VGA_WIDTH + cursorCol;
    uint16_t entry = videoMemory[index];
    uint8_t ch = entry & 0xFF;
    videoMemory[index] = vgaEntry(ch, currentColor);
    vgaOutputFlush();
}

void vgaFillLineColor(void) {
    // 直接写显存填充本行，不移动光标，
    // 避免最后一列写空格触发换行/滚动造成整行空行(间隙)
    uint8_t row, col;
    vgaGetCursorPos(&row, &col);

    size_t index = (size_t)row * VGA_WIDTH + col;
    for (int i = col; i < VGA_WIDTH; i++)
        videoMemory[index++] = vgaEntry(' ', currentColor);
    vgaOutputFlush();
}

void vgaClear(void) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(' ', currentColor);
        }
    }
    scrollCount = 0;
    scrollViewOffset = 0;
    cursorRow = 0;
    cursorCol = 0;
    vgaSetCursorPos(0, 0);
    vgaOutputFlush();
}

void vgaClearColor(void) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(videoMemory[index], currentColor);
        }
    }
    vgaOutputFlush();
}

void vgaClearFgColor(void) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(videoMemory[index], currentColor>>4);
        }
    }
    vgaOutputFlush();
}

void vgaClearBgColor(void) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(videoMemory[index], (currentColor&0x0F)<<4);
        }
    }
    vgaOutputFlush();
}

void vgaClearChar(char c) {
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            size_t index = row * VGA_WIDTH + col;
            videoMemory[index] = vgaEntry(c, videoMemory[index+1]);
        }
    }
    vgaOutputFlush();
}

void vgaPutChar(char c) {
    vgaPutCharColor(c, currentColor);
}

void vgaPutCharColor(char c, uint8_t color) {
    if (c == '\n') {
        captureRow(cursorRow);
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
        captureRow(cursorRow);
        cursorCol = 0;
        cursorRow++;
        if (cursorRow >= VGA_HEIGHT) {
            vgaScroll(1);
            cursorRow = VGA_HEIGHT - 1;
        }
    }
    vgaSetCursorPos(cursorRow, cursorCol);
    if (sBatch == 0)
        vgaOutputFlush();
}

void vgaPutStr(const char* str) {
    vgaPutStrColor(str, currentColor);
}

void vgaPutStrColor(const char* str, uint8_t color) {
    sBatch++;
    while (*str) {
        vgaPutCharColor(*str++, color);
    }
    sBatch--;
    vgaOutputFlush();
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
    vgaOutputFlush();
}

void vgaSetColor(uint8_t foreground, uint8_t background) {
    currentColor = vgaEntryColor(foreground, background);
}

uint8_t vgaGetColor(void) {
    return currentColor;
}

void putDecimal(uint32_t value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        vgaPutChar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0) {
        vgaPutChar(buf[--i]);
    }
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