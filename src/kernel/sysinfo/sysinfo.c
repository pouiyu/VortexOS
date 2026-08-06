#include "sysinfo.h"
#include "config.h"
#include <vga.h>
#include <keyboard.h>

void showSystemInfo(void) {
    vgaClear();

    drawTitle("System Info");
    vgaPutStr("OS:                   " OS_NAME);
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
    vgaPutStr("Don't know");
    messageBox("");
}