#include "serial.h"
#include <io.h>

#define COM1_PORT 0x3F8

void serialInit(void) {
    outb(COM1_PORT + 1, 0x00);  // 关中断
    outb(COM1_PORT + 3, 0x80);  // DLAB
    outb(COM1_PORT + 0, 0x03);  // 38400 波特率低字节
    outb(COM1_PORT + 1, 0x00);  // 高字节
    outb(COM1_PORT + 3, 0x03);  // 8N1
    outb(COM1_PORT + 2, 0xC7);  // FIFO
    outb(COM1_PORT + 4, 0x0B);  // DTR/RTS
}

void serialPutChar(char c) {
    while (!(inb(COM1_PORT + 5) & 0x20));  // 等待发送缓冲空
    outb(COM1_PORT, c);
}

void serialPutStr(const char* str) {
    while (*str) {
        serialPutChar(*str++);
    }
}

void serialPutHex8(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    serialPutChar(hex[(value >> 4) & 0x0F]);
    serialPutChar(hex[value & 0x0F]);
}

void serialPutHex16(uint16_t value) {
    serialPutHex8((value >> 8) & 0xFF);
    serialPutHex8(value & 0xFF);
}

void serialPutHex32(uint32_t value) {
    serialPutHex16((value >> 16) & 0xFFFF);
    serialPutHex16(value & 0xFFFF);
}