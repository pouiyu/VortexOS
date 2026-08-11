#include "ata.h"
#include <io.h>

static void ataWaitReady(void) {
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY);
}

static void ataWaitData(void) {
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ));
}

void ataInit(void) {
    // 简单探测，QEMU 下不需要复杂初始化
}

int ataReadSector(uint32_t lba, uint8_t* buf) {
    ataWaitReady();
    outb(ATA_PORT_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_COUNT, 1);
    outb(ATA_PORT_LBA_LO, lba & 0xFF);
    outb(ATA_PORT_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PORT_CMD, ATA_CMD_READ);

    ataWaitData();
    for (int i = 0; i < 256; i++) {
        uint16_t val = inw(ATA_PORT_DATA);
        buf[i * 2]     = val & 0xFF;
        buf[i * 2 + 1] = (val >> 8) & 0xFF;
    }
    return 0;
}

int ataWriteSector(uint32_t lba, const uint8_t* buf) {
    ataWaitReady();
    outb(ATA_PORT_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_COUNT, 1);
    outb(ATA_PORT_LBA_LO, lba & 0xFF);
    outb(ATA_PORT_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PORT_CMD, ATA_CMD_WRITE);

    ataWaitData();
    for (int i = 0; i < 256; i++) {
        uint16_t val = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_PORT_DATA, val);
    }
    return 0;
}