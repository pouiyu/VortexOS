// disk.h
// 磁盘抽象层：把 legacy IDE(PIO) 与 AHCI(SATA) 两类磁盘统一成 dDrive 对象，
// 提供按"整盘绝对 LBA"的扇区读写，并附带 MBR 分区表解析。
#ifndef _DRIVERS_DISK_H
#define _DRIVERS_DISK_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRIVE_TYPE_NONE = 0,
    DRIVE_TYPE_PIO_IDE,   /* legacy IDE 主/次通道，PIO */
    DRIVE_TYPE_AHCI,      /* SATA 经 AHCI HBA */
    DRIVE_TYPE_ATAPI      /* 光驱(CD/DVD)，PIO ATAPI */
} DriveType;

/* MBR 主分区条目(partition table slot 0..3) */
typedef struct {
    uint32_t startLba;    /* 分区起始 LBA(整盘绝对) */
    uint32_t numSectors;  /* 分区扇区数 */
    uint8_t  type;        /* 0x0C=FAT32 LBA / 0x07=NTFS/exFAT / 0x00=空 */
    uint8_t  bootable;    /* 0x80=带引导标志 (MBR 条目字节0) */
    uint8_t  slot;        /* MBR 槽位 0..3 */
} Partition;

/* 一个磁盘设备(控制器+通道/端口 解析而来) */
typedef struct dDrive {
    DriveType type;
    bool      present;
    uint32_t  capacityLba;   /* 整盘扇区数 */

    /* PIO IDE 后端 */
    uint16_t  baseIO;        /* 0x1F0(主) 或 0x170(次) */
    uint8_t   driveSel;      /* 0xE0(主) / 0xF0(从)，写入 DRIVE 寄存器高4位 */

    /* AHCI 后端 */
    uintptr_t abar;          /* BAR5 identity 映射后的虚地址(=物理) */
    uint8_t   port;          /* HBA 端口号 0..31 */

    /* MBR 分区表(若已扫描) */
    Partition parts[4];
    int       partCount;
} dDrive;

#define MAX_DRIVES 8

void        diskInit(void);                       /* 枚举 AHCI/IDE，填全局 gDrives[] */
int         diskGetCount(void);
dDrive*     diskGetDrive(int idx);
int         diskAppendDrive(const dDrive* drv);   /* 供后端(AHCI)追加探测到的盘，返回索引或-1 */

/* 按整盘绝对 LBA 读写；AHCI 支持 cnt 批量，PIO 每次 1 扇区(自动循环) */
int driveReadSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, uint8_t* buf);
int driveWriteSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, const uint8_t* buf);

/* MBR 分区扫描：读 LBA0，区 0x55AA，填 drv->parts[]，返回分区数 */
int diskScanPartitions(dDrive* drv);

/* 把分区条目写入 MBR 扇区第 slot 个表项，并置 0x55AA 尾部签名 */
void mbrSetEntry(uint8_t* sec, int slot, const Partition* p);

#endif