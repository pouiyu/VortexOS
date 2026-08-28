#include "ata.h"
#include <io.h>

static int ataWaitReady(void) {
    uint32_t tries = 0;
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        if (++tries > ATA_TIMEOUT_TRIES) return -1;   /* 超时，防止无磁盘时卡死 */
    }
    return 0;
}

static int ataWaitData(void) {
    uint32_t tries = 0;
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ)) {
        if (++tries > ATA_TIMEOUT_TRIES) return -1;
    }
    return 0;
}

void ataInit(void) {
    // 简单探测，QEMU 下不需要复杂初始化
}

int ataReadSector(uint32_t lba, uint8_t* buf) {
    if (ataWaitReady() != 0) return -1;
    outb(ATA_PORT_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_COUNT, 1);
    outb(ATA_PORT_LBA_LO, lba & 0xFF);
    outb(ATA_PORT_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PORT_CMD, ATA_CMD_READ);

    if (ataWaitData() != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t val = inw(ATA_PORT_DATA);
        buf[i * 2]     = val & 0xFF;
        buf[i * 2 + 1] = (val >> 8) & 0xFF;
    }
    /* 等待本次读盘完成(BSY 清零)，确保设备回到 idle，供下一条命令使用 */
    if (ataWaitReady() != 0) return -1;
    return 0;
}

int ataWriteSector(uint32_t lba, const uint8_t* buf) {
    if (ataWaitReady() != 0) return -1;
    outb(ATA_PORT_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_COUNT, 1);
    outb(ATA_PORT_LBA_LO, lba & 0xFF);
    outb(ATA_PORT_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PORT_CMD, ATA_CMD_WRITE);

    if (ataWaitData() != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t val = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_PORT_DATA, val);
    }
    /* 写盘属于设备后端操作，必须在返回前等 BSY 清零，否则紧跟的
     * 连续写入(如格式化)会在设备仍忙时被 ataWaitReady 超时打回 */
    return ataWaitReady() == 0 ? 0 : -1;
}