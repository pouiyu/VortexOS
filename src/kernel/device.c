// device.c
#include "device.h"
#include <io.h>
#include <vga.h>
#include <kernel.h>

void deviceShutdown(void) {
    vgaClear();
    vgaSetCursorPos(0, 0);
    vgaSetColorByte(theme);
    vgaPutStr("You can now safely turn off your device\n");
    __asm__ volatile ("cli");
    __asm__ volatile ("hlt");
}

void deviceReboot(void) {
    outb(0x64, 0xFE);
}