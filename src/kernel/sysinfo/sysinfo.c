#include "sysinfo.h"
#include "config.h"
#include <stdio/vga.h>
#include <keyboard.h>
#include <kernel.h>
#include <stdio/vbe.h>

/* 输出无前导零的十进制数 */
static void sysinfoPutDec(uint32_t value) {
    char buf[12];
    int i = 0;
    if (value == 0) { vgaPutChar('0'); return; }
    while (value) { buf[i++] = (char)('0' + value % 10); value /= 10; }
    while (i--) vgaPutChar(buf[i]);
}

void showSystemInfo(void) {
    vgaClear();

    drawTitle("System Info");
    showSystemLogo();
    vgaPutStr("OS:                   ");
    vgaPutStrColor(OS_NAME, HL);
    vgaPutChar('\n');
    vgaPutStr("Version:              " OS_VERSION);
    vgaPutChar('\n');
    vgaPutStr("Arch:                 " OS_ARCH);
    vgaPutChar('\n');
    vgaPutStr("ArchBits:             " OS_ARCH_BITS);
    vgaPutChar('\n');
    vgaPutStr("Boot:                 " OS_BOOT);
    vgaPutChar('\n');
    vgaPutStr("Build Date And Time:  " OS_BUILD_DATETIME);
    messageBox("");
}

void showDeviceInfo(void) {
    vgaClear();

    drawTitle("Device Info");
    if (vbeInit() == 0) {
        vbeSyncInfo();
        vgaPutStr("Video (VBE/LFB):\n");
        vgaPutStr("  Resolution   : ");
        if (gVbeInfo.enabled) {
            sysinfoPutDec(gVbeInfo.xres);
            vgaPutChar('x');
            sysinfoPutDec(gVbeInfo.yres);
        } else {
            vgaPutStr("80x25");
        }
        vgaPutChar('\n');
        vgaPutStr("  Depth        : ");
        if (gVbeInfo.enabled) {
            sysinfoPutDec(gVbeInfo.bpp);
            vgaPutStr(" bpp\n");
        } else {
            vgaPutStr("text (VGA font)\n");
        }
        vgaPutStr("  Vram         : ");
        sysinfoPutDec(gVbeInfo.vramSize / (1024u * 1024u));
        vgaPutStr(" MB\n");
        vgaPutStr("  Framebuffer  : 0x");
        vgaPutHex32(gVbeInfo.lfbAddr);
        vgaPutChar('\n');
        if (gVbeInfo.enabled)
            vgaPutStr("  Mode         : graphics\n");
        else
            vgaPutStr("  Mode         : text\n");
    } else {
        vgaPutStr("Video: not detected\n");
    }
    messageBox("");
}

void showSystemLogo(void) {
    vgaClear();
    vgaPutStrColor(OS_LOGO, COLOR_BLUE);
    //messageBox("");
}