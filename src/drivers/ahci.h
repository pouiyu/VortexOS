// ahci.h
// AHCI(SATA) 主机控制器后端：探测 PCI 上的 AHCI 设备、复位控制器、
// 对活动端口做 PIO-free 的 DMA 扇区读写（48 位 LBA）。
// 接入 disk.c 的 dDrive 抽象（DRIVE_TYPE_AHCI）。
#ifndef _DRIVERS_AHCI_H
#define _DRIVERS_AHCI_H

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"

/* 初始化：枚举 PCI 上的 AHCI(SATA) 控制器，探测活动端口并挂入磁盘抽象层。
   返回加入磁盘数，0 表示无 AHCI 盘。 */
int ahciProbe(void);

/* 批量扇区读写（整盘绝对 LBA）。cnt 通常一次传输的扇区数（≤256/命令）。 */
int ahciReadSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, uint8_t* buf);
int ahciWriteSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, const uint8_t* buf);

#endif