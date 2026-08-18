#ifndef _DRIVERS_SERIAL_H
#define _DRIVERS_SERIAL_H

#include <stdint.h>

void serialInit(void);
void serialPutChar(char c);
void serialPutStr(const char* str);

#endif