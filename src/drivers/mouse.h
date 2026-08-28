#ifndef _DRIVERS_MOUSE_H
#define _DRIVERS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_DATA_PORT       0x60
#define MOUSE_COMMAND_PORT    0x64
#define MOUSE_IRQ             12
#define MOUSE_INT_VECTOR      0x2C

#define MOUSE_LEFT_BUTTON     0x01
#define MOUSE_RIGHT_BUTTON    0x02
#define MOUSE_MIDDLE_BUTTON   0x04

/* 兼容 8042 的自检/命令 */
#define MOUSE_CMD_WRITE_AUX   0xD4   // 指示 0x60 的下一个字节写给辅助设备
#define MOUSE_CMD_ENABLE_AUX  0xA8   // 启用辅助(鼠标)设备

/* 鼠标自身命令(经 MOUSE_CMD_WRITE_AUX 发送) */
#define MOUSE_DEV_SET_DEFAULTS  0xF6
#define MOUSE_DEV_ENABLE_REPORT 0xF4

/* 标准 3 字节包: b0 为标志字节, b1=X 位移, b2=Y 位移 */
#define MOUSE_PACKET_SIZE 3
#define MOUSE_B0_ALWAYS    0x08  // bit3 恒为 1，可作帧同步
#define MOUSE_B0_Y_OVERFLOW 0x80
#define MOUSE_B0_X_OVERFLOW 0x40
#define MOUSE_B0_Y_SIGN    0x20
#define MOUSE_B0_X_SIGN    0x10

void mouseInit(void);
void mouseIRQHandler(void);

bool  mouseHasEvent(void);
uint8_t mouseGetButtons(void);
int   mouseGetX(void);
int   mouseGetY(void);
void  mouseGetMotion(int* dx, int* dy);   // 取出并清零累积位移
void  mouseSetPosition(int x, int y);
void  mouseSetBounds(int w, int h);       // 限制光标范围(屏幕尺寸)

#endif