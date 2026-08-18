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