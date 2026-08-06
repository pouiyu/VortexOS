#ifndef _KERNEL_KERNEL_H
#define _KERNEL_KERNEL_H

#include <stdint.h>
#include <sys/cdefs.h>

extern uint8_t theme;

void vgaSetColorByte(uint8_t color);
void messageBox(const char* msg);
void drawTitle(const char* title);
void drawMainMenu(void);

#endif