#ifndef _KERNEL_KERNEL_H
#define _KERNEL_KERNEL_H

#include <stdint.h>
#include <sys/cdefs.h>
#include <serial.h>

extern uint8_t theme;
extern uint8_t FG;
extern uint8_t BG;
extern uint8_t HL;
extern uint8_t LL;
extern unsigned int colorCount;

void vgaSetColorByte(uint8_t color);
void messageBox(const char* msg);
void drawTitle(const char* title);
void drawMainMenu(void);
void updateTheme(void);

#define LOG(msg) serialPutStr("[") serialPutStr(__func__) serialPutStr("] ") serialPutStr(msg) serialPutStr("\n")

#endif