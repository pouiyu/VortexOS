// disk.c
// 磁盘抽象层实现：PIO IDE 后端 + 通用分发 + MBR 分区解析。
// AHCI 后端在 ahci.c，通过 ahciReadSectors/ahciWriteSectors 接入。
#include "disk.h"
#include "ahci.h"
#include <io.h>
#include <serial.h>
#include <string/string.h>

/* ===================== PIO IDE 寄存器与状态 ===================== */
#define IDEB_DATA     0
#define IDEB_COUNT    2
#define IDEB_LBA_LO   3
#define IDEB_LBA_MID  4
#define IDEB_LBA_HI   5
#define IDEB_DRIVE    6
#define IDEB_CMD      7
#define IDEB_STATUS   7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC

#define IDE_TIMEOUT_TRIES 100000   /* 真机无盘通道读 0xFF(BSY) 时的有界等待(约 0.1s) */

static dDrive gDrives[MAX_DRIVES];
static int    gDriveCount = 0;

static void ideDelay(void) {
    for (volatile int i = 0; i < 16; i++) __asm__ volatile ("nop");
}

/* 等待控制器不忙(一次命令的收尾)；超时返回 -1 */
static int ideWaitNotBusy(uint16_t base) {
    uint32_t tries = 0;
    for (;;) {
        uint8_t st = inb(base + IDEB_STATUS);
        if (!(st & ATA_SR_BSY)) return 0;
        if (st == 0xFF) return -1;      /* 悬浮总线(无设备) */
        if (++tries > IDE_TIMEOUT_TRIES) return -1;
    }
}

/* 命令发出后等待 DRQ(数据就绪)；若 ERR 直接失败 */
static int ideWaitForData(uint16_t base) {
    uint32_t tries = 0;
    for (;;) {
        uint8_t st = inb(base + IDEB_STATUS);
        if (!(st & ATA_SR_BSY)) {
            if (st & ATA_SR_ERR) return -1;
            if (st & ATA_SR_DRQ) return 0;
        }
        if (st == 0xFF) return -1;      /* 悬浮总线(无设备) */
        if (++tries > IDE_TIMEOUT_TRIES) return -1;
    }
}

static int ideReadSector(const dDrive* d, uint32_t lba, uint8_t* buf) {
    uint16_t base = d->baseIO;
    if (ideWaitNotBusy(base) != 0) return -1;
    outb(base + IDEB_DRIVE, (d->driveSel) | ((lba >> 24) & 0x0F));
    outb(base + IDEB_COUNT, 1);
    outb(base + IDEB_LBA_LO, lba & 0xFF);
    outb(base + IDEB_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + IDEB_LBA_HI, (lba >> 16) & 0xFF);
    outb(base + IDEB_CMD, ATA_CMD_READ);
    if (ideWaitForData(base) != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(base + IDEB_DATA);
        buf[i * 2] = (uint8_t)(w & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    ideWaitNotBusy(base);
    return 0;
}

static int ideWriteSector(const dDrive* d, uint32_t lba, const uint8_t* buf) {
    uint16_t base = d->baseIO;
    if (ideWaitNotBusy(base) != 0) return -1;
    outb(base + IDEB_DRIVE, (d->driveSel) | ((lba >> 24) & 0x0F));
    outb(base + IDEB_COUNT, 1);
    outb(base + IDEB_LBA_LO, lba & 0xFF);
    outb(base + IDEB_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + IDEB_LBA_HI, (lba >> 16) & 0xFF);
    outb(base + IDEB_CMD, ATA_CMD_WRITE);
    if (ideWaitForData(base) != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(base + IDEB_DATA, w);
    }
    ideWaitNotBusy(base);
    return 0;
}

/* 探测(通道, 主/从)：IDENTIFY 判定存在并取 LBA28 容量；返回扇区数，无盘返回 0 */
static uint32_t ideProbeDrive(uint16_t baseIO, uint8_t driveSel) {
    uint16_t base = baseIO;
    /* 标准悬浮总线检测：先选中目标驱动器再读状态。空通道读回 0xFF(数据总线上拉
     * 全置位)判定无设备；0x00 可能是设备复位未完成的瞬态(慢盘/QEMU TCG 常见)，
     * 不能跳过，交给下面有界超时的 IDENTIFY 等待收尾，避免漏检真实硬盘。 */
    outb(base + IDEB_DRIVE, driveSel);
    ideDelay();
    uint8_t st = inb(base + IDEB_STATUS);
    if (st == 0xFF) return 0;
    if (ideWaitNotBusy(base) != 0) return 0;
    outb(base + IDEB_CMD, ATA_CMD_IDENTIFY);
    if (ideWaitForData(base) != 0) return 0;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(base + IDEB_DATA);
    ideWaitNotBusy(base);

    /* LBA28 容量 = Word 60/61 */
    uint32_t cap = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (cap == 0) cap = 1024;   /* 兜底 */
    return cap;
}

/* 探测一条 legacy IDE 通道(主+从) */
static void ideProbeChannel(uint16_t baseIO, const char* tag) {
    static const uint8_t devs[2] = { 0xE0, 0xF0 };

    for (int s = 0; s < 2; s++) {
        uint32_t cap = ideProbeDrive(baseIO, devs[s]);
        if (cap == 0) continue;

        dDrive d;
        memset(&d, 0, sizeof(d));
        d.type        = DRIVE_TYPE_PIO_IDE;
        d.present     = true;
        d.capacityLba = cap;
        d.baseIO      = baseIO;
        d.driveSel    = devs[s];
        d.port        = (uint8_t)s;
        if (diskAppendDrive(&d) >= 0) {
            serialPutStr("[DISK] "); serialPutStr(tag);
            serialPutStr(s ? " slave: " : " master: ");
            /* 十六进制容量打印(无十进制辅助时宽验) */
            /* 兼容：此处仅输出标记，容量由后续分区扫描体现 */
            serialPutStr("IDE present\n");
        }
    }
}

/* ===================== 通用分发 ===================== */

int diskAppendDrive(const dDrive* drv) {
    if (gDriveCount >= MAX_DRIVES || !drv || !drv->present) return -1;
    gDrives[gDriveCount] = *drv;
    return gDriveCount++;
}

int diskGetCount(void) {
    return gDriveCount;
}

dDrive* diskGetDrive(int idx) {
    if (idx < 0 || idx >= gDriveCount) return NULL;
    return &gDrives[idx];
}

void diskInit(void) {
    gDriveCount = 0;
    memset(gDrives, 0, sizeof(gDrives));

    /* 1. legacy IDE 两条通道(真机/VMware IDE、QEMU pc 均在此) */
    ideProbeChannel(0x1F0, "IDE0");
    ideProbeChannel(0x170, "IDE1");

    /* 2. AHCI(SATA) 经 PCI 枚举 */
    ahciProbe();

    serialPutStr("[DISK] total drives: ");
    /* 十进制输出助手(逐位) */
    {
        char tmp[12]; int i = 0; int v = gDriveCount;
        if (v == 0) tmp[i++] = '0';
        while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
        while (i--) serialPutStr((char[]){ tmp[i], 0 });
    }
    serialPutStr("\n");
}

int driveReadSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, uint8_t* buf) {
    if (!drv || !drv->present) return -1;
    switch (drv->type) {
        case DRIVE_TYPE_PIO_IDE:
            for (uint32_t i = 0; i < cnt; i++) {
                if (ideReadSector(drv, lba + i, buf + (size_t)i * 512) != 0) return -1;
            }
            return 0;
        case DRIVE_TYPE_AHCI:
            return ahciReadSectors(drv, lba, cnt, buf);
        default:
            return -1;
    }
}

int driveWriteSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, const uint8_t* buf) {
    if (!drv || !drv->present) return -1;
    switch (drv->type) {
        case DRIVE_TYPE_PIO_IDE:
            for (uint32_t i = 0; i < cnt; i++) {
                if (ideWriteSector(drv, lba + i, buf + (size_t)i * 512) != 0) return -1;
            }
            return 0;
        case DRIVE_TYPE_AHCI:
            return ahciWriteSectors(drv, lba, cnt, buf);
        default:
            return -1;
    }
}

/* ===================== MBR 分区解析 ===================== */

int diskScanPartitions(dDrive* drv) {
    if (!drv || !drv->present) return -1;
    drv->partCount = 0;

    uint8_t mbr[512];
    if (driveReadSectors(drv, 0, 1, mbr) != 0) return -1;
    if (!(mbr[510] == 0x55 && mbr[511] == 0xAA)) return 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t* e = mbr + 446 + 16 * i;
        Partition p;
        p.bootable   = e[0];                 /* 0x80 */
        p.type       = e[4];
        p.startLba   = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                       ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        p.numSectors = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                       ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        p.slot       = (uint8_t)i;
        if (p.type != 0 && p.startLba >= 1 && p.numSectors > 0) {
            drv->parts[drv->partCount++] = p;
        }
    }
    return drv->partCount;
}

void mbrSetEntry(uint8_t* sec, int slot, const Partition* p) {
    if (slot < 0 || slot > 3) return;
    uint8_t* e = sec + 446 + 16 * slot;
    memset(e, 0, 16);
    e[0] = p->bootable;                  /* 0x80 或 0 */
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;   /* CHS(忽略) */
    e[4] = p->type;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    e[8]  = (uint8_t)(p->startLba);
    e[9]  = (uint8_t)(p->startLba >> 8);
    e[10] = (uint8_t)(p->startLba >> 16);
    e[11] = (uint8_t)(p->startLba >> 24);
    e[12] = (uint8_t)(p->numSectors);
    e[13] = (uint8_t)(p->numSectors >> 8);
    e[14] = (uint8_t)(p->numSectors >> 16);
    e[15] = (uint8_t)(p->numSectors >> 24);
    sec[510] = 0x55;
    sec[511] = 0xAA;
}