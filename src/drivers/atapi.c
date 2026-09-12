// atapi.c
// ATAPI(CD-ROM) PIO 驱动：通过 legacy IDE 通道(0x1F0/0x170)以数据包命令读取 2048 字节逻辑扇区。
// 采用 PIO 方式，QEMU i440fx/Bochs 及绝大多数真实 IDE 光驱都可识别。
#include "atapi.h"
#include <io.h>
#include <serial.h>

/* 尝试次数上限：真机上无设备通道读状态返回 0xFF(=BSY 全置位)会空转很久，
 * 过大的上限会让人误判为死机。100000 次足够慢速光驱响应，空通道也能快速退出。 */
#define ATAPI_TIMEOUT 100000

/* 探测到的光驱所在通道的命令端口与设备选择位 */
static uint16_t sCmdBase = 0;
static uint8_t  sDevSel  = 0xA0;   /* 0xA0=master, 0xB0=slave */
static bool     sReady   = false;
static bool     sSelected = false; /* 设备选择已建立，连续读无需每次重选+延时 */

/* 给硬件一点响应时间(迁就慢速光驱) */
static void atapiDelay(void) {
    for (volatile uint32_t i = 0; i < 20000; i++) {
        __asm__ volatile ("");
    }
}

/* 等待 BSY 清零；返回 0 成功，-1 超时 */
static int atapiWaitNotBusy(void) {
    uint32_t tries = 0;
    while (inb(sCmdBase + 7) & 0x80) {
        if (++tries > ATAPI_TIMEOUT) return -1;
    }
    return 0;
}

/* 等待 DRQ 就绪；返回 0 成功，-1 出错或超时 */
static int atapiWaitData(void) {
    uint32_t tries = 0;
    for (;;) {
        uint8_t st = inb(sCmdBase + 7);
        if ((st & 0x80) == 0) {        /* 不忙 */
            if (st & 0x08) return 0;   /* DRQ 就绪 */
            if (st & 0x01) return -1;  /* 出错 */
        }
        if (++tries > ATAPI_TIMEOUT) return -1;
    }
}

/* 探测指定通道上的 master/slave 是否为 ATAPI(CD-ROM) 设备。
 * 方法：向该槽位发 IDENTIFY PACKET DEVICE(0xA1)，随后读取状态：
 *   - 空槽(状态=0)                      -> 跳过
 *   - ATA 硬盘(不接受包命令，置 ERR)     -> 跳过
 *   - ATAPI 光驱(接受并置 DRQ、无 ERR)   -> 命中 */
static int probeChannel(uint16_t base) {
    static const uint8_t sels[2] = { 0xA0, 0xB0 };
    for (int d = 0; d < 2; d++) {
        outb(base + 6, sels[d]);          /* 选择设备 */
        atapiDelay();
        outb(base + 7, 0xA1);             /* IDENTIFY PACKET DEVICE */
        atapiDelay();

        /* 悬浮总线(无设备)快速判定：读状态恒为 0xFF(=BSY 全置位)。
         * 逐个槽位都烧满 ATAPI_TIMEOUT 空转会让真机启动显得"卡死"。 */
        if (inb(base + 7) == 0xFF) continue;

        uint32_t guard = 0;
        while ((inb(base + 7) & 0x80) && (++guard < ATAPI_TIMEOUT)) {}
        if (guard >= ATAPI_TIMEOUT) {     /* 浮动/持续忙(视为空槽) */
            serialPutStr("[ATAPI] slot busy, skip\n");
            continue;
        }

        uint8_t st = inb(base + 7);
        if (st == 0x00 || (st & 0x01)) {  /* 空槽 或 ATA 设备(出错) */
            continue;
        }
        /* 接受了包命令：ATAPI 设备 */
        serialPutStr("[ATAPI] found ch=");
        serialPutHex16(base);
        serialPutStr(" sel=");
        serialPutHex8(sels[d]);
        serialPutStr(" st=");
        serialPutHex8(st);
        serialPutStr("\n");
        sCmdBase = base;
        sDevSel  = sels[d];

        /* 排空 IDENTIFY PACKET DEVICE 返回的 512 字节数据，清除 DRQ，
         * 否则设备停留在 IDENTIFY 数据阶段，后续 READ(10) 无法启动。
         * 用固定 256 字读取，不依赖字节计数组(其在检测时可能尚未就绪)。 */
        uint32_t guard2 = 0;
        while ((inb(base + 7) & 0x80) && (++guard2 < ATAPI_TIMEOUT)) {}
        for (uint32_t i = 0; i < 256; i++) {
            (void)inw(base);
        }
        (void)atapiWaitNotBusy();
        serialPutStr("[ATAPI] drained identify\n");
        return 1;
    }
    return 0;
}

void atapiInit(void) {
    /* 光驱通常在 secondary master；primary 通常是硬盘(ATA) */
    if (probeChannel(0x170)) { serialPutStr("[ATAPI] CD on 0x170\n"); sReady = true; return; }
    if (probeChannel(0x1F0)) { serialPutStr("[ATAPI] CD on 0x1F0\n"); sReady = true; return; }
    sReady = false;
    serialPutStr("[ATAPI] no CD found\n");
}

bool atapiReady(void) {
    return sReady;
}

/* 等待 DRQ 并读取设备返回的字节计数 */
static int atapiReadDataPhase(uint8_t* buf, uint32_t* outBc) {
    if (atapiWaitData() != 0) return -1;

    uint32_t bc = ((uint32_t)inb(sCmdBase + 5) << 8) | inb(sCmdBase + 4);
    if (bc == 0 || bc > ATAPI_BLOCK_SIZE) bc = ATAPI_BLOCK_SIZE;

    for (uint32_t i = 0; i < bc / 2; i++) {
        uint16_t w = inw(sCmdBase);
        buf[i * 2]     = (uint8_t)(w & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    if (outBc) *outBc = bc;
    return 0;
}

int atapiReadBlock(uint32_t lba, uint8_t* buf) {
    if (!sReady || !buf) return -1;

    /* 仅首次需要选择设备并给它响应时间；连续读时设备选择保持，跳过 2 万次延时循环 */
    if (!sSelected) {
        outb(sCmdBase + 6, sDevSel);
        atapiDelay();
        sSelected = true;
    }
    if (atapiWaitNotBusy() != 0) return -1;

    outb(sCmdBase + 1, 0);   /* features: PIO */
    /* ATAPI PIO 下须在发包前由主控设置字节数上限，否则设备对后续数据阶段报 ABRT。
     * 上限放在 Cylinder Low(base+4) 与 Cylinder High(base+5)，共 16 位，单位是字节。
     * 2048 = 0x0800 => 低字节 0x00(Cylinder Low) + 高字节 0x08(Cylinder High) */
    outb(sCmdBase + 4, 0x00);
    outb(sCmdBase + 5, 0x08);
    outb(sCmdBase + 2, 0);

    outb(sCmdBase + 7, 0xA0);   /* PACKET 命令 */
    if (atapiWaitData() != 0) {
        serialPutStr("[ATAPI] cdb-accept DRQ timeout st=");
        serialPutHex8(inb(sCmdBase + 7));
        serialPutStr("\n");
        return -1;
    }

    /* 12 字节 READ(10) 包：读 1 个 2048 字节逻辑扇区 */
    uint8_t pkt[12];
    pkt[0]  = 0xA8;
    pkt[1]  = 0x00;
    pkt[2]  = (uint8_t)(lba >> 24);
    pkt[3]  = (uint8_t)(lba >> 16);
    pkt[4]  = (uint8_t)(lba >> 8);
    pkt[5]  = (uint8_t)(lba);
    pkt[6]  = 0x00;
    pkt[7]  = 0x00;
    pkt[8]  = 0x00;
    pkt[9]  = 0x01;
    pkt[10] = 0x00;
    pkt[11] = 0x00;

    for (int i = 0; i < 6; i++) {
        uint16_t w = (uint16_t)pkt[i * 2] | ((uint16_t)pkt[i * 2 + 1] << 8);
        outw(sCmdBase, w);
    }

    /* 等待数据阶段 */
    uint32_t bc = 0;
    if (atapiReadDataPhase(buf, &bc) != 0) {
        serialPutStr("[ATAPI] data DRQ timeout st=");
        serialPutHex8(inb(sCmdBase + 7));
        serialPutStr(" err=");
        serialPutHex8(inb(sCmdBase + 1));
        serialPutStr("\n");
        return -1;
    }

    /* 读状态寄存器以确认中断，然后等待 BSY 清零 */
    (void)inb(sCmdBase + 7);
    (void)atapiWaitNotBusy();
    return 0;
}