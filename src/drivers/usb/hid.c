#include "hid.h"
#include "xhci.h"
#include <keyboard.h>
#include <serial.h>
#include <stdio/vga.h>
#include <string/string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================ USB HID 用法 → PS/2 扫描码 ============================
 * 表项编码：低 8 位为 PS/2 产生码（make code），bit8 置位表示该键需先发 0xE0 前缀。
 * 0 表示未映射（忽略）。仅覆盖键盘 Boot Report 常用用法。 */
static const uint16_t usagePs2[0xE8] = {
    /* A-Z */
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21,
    [0x0A] = 0x22, [0x0B] = 0x23, [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13,
    [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    /* 数字行 */
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06,
    [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B,
    /* 编辑区 / 标点 */
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F, [0x2C] = 0x39,
    [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B,
    [0x33] = 0x27, [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35,
    [0x39] = 0x3A,
    /* F1-F12 */
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F,
    [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44,
    [0x44] = 0x57, [0x45] = 0x58,
    [0x47] = 0x46,
    /* 导航键（需 0xE0 前缀） */
    [0x49] = 0x152, [0x4A] = 0x147, [0x4B] = 0x149, [0x4C] = 0x153, [0x4D] = 0x14F,
    [0x4E] = 0x151, [0x4F] = 0x14D, [0x50] = 0x14B, [0x51] = 0x150, [0x52] = 0x148,
    /* 小键盘 */
    [0x53] = 0x45, [0x54] = 0x135, [0x55] = 0x37, [0x56] = 0x4A, [0x57] = 0x4E,
    [0x58] = 0x11C,
    [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C,
    [0x5E] = 0x4D, [0x5F] = 0x47, [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53,
    [0x65] = 0x15D,
    /* 修饰键 */
    [0xE0] = 0x1D, [0xE1] = 0x2A, [0xE2] = 0x38, [0xE3] = 0x15B,
    [0xE4] = 0x11D, [0xE5] = 0x36, [0xE6] = 0x138, [0xE7] = 0x15C,
};

/* 当前按下状态，用于按/放检测（支持 6 键滚动） */
static bool hidHeld[0xE8];

/* 注入一个已编码的 PS/2 扫描码（make/release） */
static void hidInject(uint16_t mapVal, bool release) {
    if (mapVal == 0) return;
    if (mapVal & 0x100) keyboardProcessScancode(0xE0);   /* 需要 E0 前缀 */
    keyboardProcessScancode((uint8_t)((mapVal & 0xFF) | (release ? 0x80 : 0)));
}

/* 键盘 Boot Report：byte0 修饰键，byte2..7 为按下键的用法码 */
static void hidKeyReport(uint8_t slotId, uint8_t* data, uint32_t len) {
    (void)slotId;
    if (len < 2) return;

    bool now[0xE8];
    memset(now, 0, sizeof(now));

    static const uint8_t modUsage[8] = { 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7 };
    uint8_t mod = data[0];
    for (int i = 0; i < 8; i++)
        if (mod & (1u << i)) now[modUsage[i]] = true;

    uint32_t n = (len > 8) ? 8 : len;
    for (uint32_t i = 2; i < n; i++) {
        uint8_t u = data[i];
        if (u && u < 0xE8) now[u] = true;
    }

    for (int u = 0; u < 0xE8; u++) {
        if (hidHeld[u] && !now[u]) {
            hidHeld[u] = false;
            hidInject(usagePs2[u], true);
        } else if (!hidHeld[u] && now[u]) {
            hidHeld[u] = true;
            hidInject(usagePs2[u], false);
        }
    }
}

/* 鼠标 Boot Report：byte0 按钮，byte1 X，byte2 Y（当前仅打印验证） */
static void hidMouseReport(uint8_t slotId, uint8_t* data, uint32_t len) {
    (void)slotId;
    if (len < 3) return;
    uint8_t btns = data[0];
    int8_t  dx = (int8_t)data[1];
    int8_t  dy = (int8_t)data[2];
    serialPutStr("[MOUSE] btns=");
    serialPutHex8(btns);
    serialPutStr(" dx=");
    serialPutHex8((uint8_t)dx);
    serialPutStr(" dy=");
    serialPutHex8((uint8_t)dy);
    serialPutStr("\n");
}

int hidLoadFromXhci(void) {
    int n = xhciGetDeviceCount();
    int kbd = 0, mse = 0;
    memset(hidHeld, 0, sizeof(hidHeld));

    for (int i = 0; i < n; i++) {
        XhciDeviceInfo info;
        if (xhciGetDeviceInfo((uint8_t)i, &info) != 0) continue;
        if (info.deviceClass != 3) continue;   /* 仅 HID 设备 */

        if (info.deviceProtocol == 1) {        /* 键盘 Boot */
            if (xhciRegisterEp1Handler(info.slotId, hidKeyReport) == 0) {
                xhciArmInterruptIn(info.slotId);
                kbd++;
            }
        } else if (info.deviceProtocol == 2) { /* 鼠标 Boot */
            if (xhciRegisterEp1Handler(info.slotId, hidMouseReport) == 0) {
                xhciArmInterruptIn(info.slotId);
                mse++;
            }
        }
    }

    serialPutStr("[HID] kbd=");
    serialPutHex8((uint8_t)kbd);
    serialPutStr(" mouse=");
    serialPutHex8((uint8_t)mse);
    serialPutStr("\n");
    vgaPutStr("[HID] input loaded\n");
    return (kbd || mse) ? 0 : -1;
}