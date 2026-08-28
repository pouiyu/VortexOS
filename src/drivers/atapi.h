#ifndef _DRIVERS_ATAPI_H
#define _DRIVERS_ATAPI_H

#include <stdint.h>
#include <stdbool.h>

/* 光驱逻辑扇区固定为 2048 字节 */
#define ATAPI_BLOCK_SIZE 2048

/* 初始化：自动在 primary/secondary 通道上探测 ATAPI(光盘) 设备 */
void atapiInit(void);

/* 是否探测到可用的光驱 */
bool atapiReady(void);

/* 读取一个 2048 字节的逻辑扇区；成功返回 0，失败返回 -1 */
int atapiReadBlock(uint32_t lba, uint8_t* buf);

#endif