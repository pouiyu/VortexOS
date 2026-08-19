#ifndef _DRIVERS_SERIAL_H
#define _DRIVERS_SERIAL_H

#include <stdint.h>

void serialInit(void);
void serialPutChar(char c);
void serialPutStr(const char* str);
void serialPutHex8(uint8_t value);
void serialPutHex16(uint16_t value);
void serialPutHex32(uint32_t value);

#endif