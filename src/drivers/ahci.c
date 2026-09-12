// ahci.c
// AHCI(SATA) 主机控制器后端实现：
//   1. ahciProbe()   —— PCI 枚举 AHCI，复位/使能 HBA，探测并挂接活动端口
//   2. ahciRead/WriteSectors —— 经 DMA(READ/WRITE_DMA_EXT, LBA48) 读写
// 寄存器访问走 identity 映射的 BAR5(ABAR)，数据/命令表/PRD 分配到 pmm 并加
// PCD|PWT(免缓存)以保证 DMA 一致性。
#include "ahci.h"
#include "usb/pci.h"
#include <mm/paging.h>
#include <mm/pmm.h>
#include <string/string.h>
#include <io.h>
#include <serial.h>

/* ===================== AHCI 全局/端口寄存器偏移 ===================== */
#define HBA_CAP     0x00   /* 能力：bit4:0 = NP(端口数), bit31 = S64A */
#define GHC         0x04   /* 全局：bit0 HR, bit1 IE, bit31 AE */
#define PPI         0x0C   /* PI: Ports Implemented */

#define PxCLB       0x00
#define PxCLBU      0x04
#define PxFB        0x08
#define PxFBU       0x0C
#define PxIS        0x10
#define PxIE        0x14
#define PxCMD       0x18
#define PxTFD       0x20
#define PxSIG       0x24
#define PxCI        0x38

#define PORTOFFSET  0x80

#define GHC_HR      (1u<<0)
#define GHC_AE      (1u<<31)

#define PICMD_ST    (1u<<0)
#define PICMD_FRE   (1u<<4)
#define PICMD_FR    (1u<<14)
#define PICMD_CR    (1u<<15)

#define TFD_BSY     (1u<<7)
#define TFD_ERR     (1u<<0)

#define ATA_CMD_READ_DMA_EXT   0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35

/* FIS 类型：H2D(Register - Host to Device) */
#define FIS_TYPE_H2D 0x27

/* 每次命令最大扇区数(PRD 单条覆盖)，缓冲分配 64 扇区 * 512 = 32KB */
#define AHCI_MAX_SECTORS 64
#define AHCI_BUF_PAGES   (AHCI_MAX_SECTORS * 512 / PAGE_SIZE)

/* 每端口私有上下文(命令列表 / 命令表 / FIS 基址 + 数据缓冲) */
typedef struct {
    bool        used;
    uintptr_t   abar;
    uint8_t     port;
    uintptr_t   clb;      /* 命令列表(1 页,32 个命令头,256B 对齐) */
    uintptr_t   ctba;     /* 命令表(1 页,含 CFIS 与 PRD 区) */
    uintptr_t   fb;       /* 接收 FIS 区(1 页) */
    uintptr_t   databuf;  /* 数据缓冲(phys=virtual) */
} AhciCtx;

static AhciCtx gAhciCtx[MAX_DRIVES];
static uint32_t gAbar = 0;

/* MMIO 读写 */
static inline uint32_t ahciRead32(uintptr_t base, uint32_t off) {
    return *(volatile uint32_t*)(base + off);
}
static inline void ahciWrite32(uintptr_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(base + off) = v;
}
static inline uintptr_t portBase(uintptr_t abar, uint8_t port) {
    return abar + 0x100 + (uintptr_t)port * PORTOFFSET;
}

static void ahciBusyWait(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) __asm__ volatile ("");
}

/* 查找指定 abar+port 的上下文 */
static AhciCtx* ahciFindCtx(uintptr_t abar, uint8_t port) {
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (gAhciCtx[i].used && gAhciCtx[i].abar == abar && gAhciCtx[i].port == port)
            return &gAhciCtx[i];
    }
    return NULL;
}

/* 分配并 remap 带 PCD|PWT 属性的 DMA 页面 */
static uintptr_t ahciAllocDma(uint32_t pages) {
    uintptr_t p = (uintptr_t)pmmAllocPages(pages);
    if (!p) return 0;
    memset((void*)p, 0, (size_t)pages * PAGE_SIZE);
    for (uint32_t i = 0; i < pages; i++)
        pagingMapPage((uint32_t)p + i * PAGE_SIZE,
                      (uint32_t)p + i * PAGE_SIZE,
                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_PCD | PAGE_PWT);
    return p;
}

/* 启动端口引擎：停止 -> 设 CLB/FB -> 使能 FIS 接收 -> 开始 */
static int ahciPortStart(AhciCtx* cx) {
    uintptr_t p = portBase(cx->abar, cx->port);

    /* 停止端口 */
    uint32_t cmd = ahciRead32(p, PxCMD);
    if (cmd & (PICMD_ST | PICMD_FRE)) {
        ahciWrite32(p, PxCMD, cmd & ~(PICMD_ST | PICMD_FRE));
        uint32_t t = 0;
        while ((ahciRead32(p, PxCMD) & (PICMD_CR | PICMD_FR)) &&
               ++t < 100000) ahciBusyWait(100);
        if (t >= 100000) { serialPutStr("[AHCI] port stop timeout\n"); return -1; }
    }

    /* 清命令完成等状态 */
    ahciWrite32(p, PxIS, 0xFFFFFFFF);

    /* 写基址 */
    ahciWrite32(p, PxCLB,  (uint32_t)cx->clb);
    ahciWrite32(p, PxCLBU, 0);
    ahciWrite32(p, PxFB,   (uint32_t)cx->fb);
    ahciWrite32(p, PxFBU,  0);
    /* 清 PRDBC(通过清 32 个命令头) */
    memset((void*)cx->clb, 0, 256);

    /* 使能 FIS 接收 */
    cmd = ahciRead32(p, PxCMD);
    ahciWrite32(p, PxCMD, cmd | PICMD_FRE);
    uint32_t t = 0;
    while (!(ahciRead32(p, PxCMD) & PICMD_FR) && ++t < 100000) ahciBusyWait(100);
    if (t >= 100000) { serialPutStr("[AHCI] FRE timeout\n"); return -1; }

    /* 开始(变 DMA 引擎) */
    cmd = ahciRead32(p, PxCMD);
    ahciWrite32(p, PxCMD, cmd | PICMD_ST);
    t = 0;
    while (!(ahciRead32(p, PxCMD) & PICMD_CR) && ++t < 100000) ahciBusyWait(100);

    /* 使能端口中断位 */
    ahciWrite32(p, PxIE, 0xFFFFFFFF);
    return 0;
}

/* 单条命令：对指定 abar+port 的一次 DMA 传输。
   write: 1=写盘(数据到设备), 0=读盘。返回 0 成功。 */
static int ahciDoCommand(AhciCtx* cx, uint64_t lba, uint32_t secs,
                         uint8_t* data, bool write) {
    if (secs == 0 || secs > AHCI_MAX_SECTORS) return -1;
    uintptr_t p = portBase(cx->abar, cx->port);

    /* 等待引擎空闲 */
    uint32_t t = 0;
    while ((ahciRead32(p, PxTFD) & TFD_BSY) && ++t < 1000000) ahciBusyWait(100);
    if (t >= 1000000) { serialPutStr("[AHCI] port busy\n"); return -1; }

    /* 命令头 slot 0 */
    uint32_t* head = (uint32_t*)cx->clb;
    head[0] = (5) | (write ? 0x40 : 0) | (1u << 16); /* CFL=5, W, PRDTL=1 */
    head[1] = 0;                                     /* PRDBC */
    head[2] = (uint32_t)cx->ctba;                    /* command table base */
    head[3] = 0;

    uint8_t* ct = (uint8_t*)cx->ctba;
    memset(ct, 0, 128);

    /* H2D 寄存器 FIS(20 字节,位于命令表 +0) */
    uint8_t* cfis = ct;
    cfis[0] = FIS_TYPE_H2D;
    cfis[1] = 0x00;                 /* PortMultiplier(无)/C=0 */
    cfis[2] = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    cfis[3] = 0x40;                 /* Device: bit6 = LBA */
    cfis[4] = (uint8_t)(lba);
    cfis[5] = (uint8_t)(lba >> 8);
    cfis[6] = (uint8_t)(lba >> 16);
    cfis[8] = (uint8_t)(lba >> 24);
    cfis[9] = (uint8_t)(lba >> 32);
    cfis[10]= (uint8_t)(lba >> 40);
    cfis[12]= (uint8_t)(secs);
    cfis[13]= (uint8_t)(secs >> 8);
    cfis[14]= 0;                    /* ICC */
    cfis[15]= 0;                    /* Control */

    /* 数据(拷贝到/从 DMA 缓冲) —— PRD 指向 cx->databuf */
    uint8_t* dbuf = (uint8_t*)cx->databuf;
    if (write) memcpy(dbuf, data, (size_t)secs * 512);

    /* PRD(命令表 +0x80) */
    uint32_t* prd = (uint32_t*)((uint8_t*)cx->ctba + 0x80);
    prd[0] = (uint32_t)cx->databuf;
    prd[1] = 0;
    prd[2] = 0;
    prd[3] = (secs * 512 - 1) & 0x3FFFFF;   /* DBC = 字节数-1 */

    __asm__ __volatile__ ("" ::: "memory");

    /* 提交命令 */
    ahciWrite32(p, PxCI, 1);          /* slot 0 */
    t = 0;
    while ((ahciRead32(p, PxCI) & 1) && ++t < 10000000) ahciBusyWait(100);
    __asm__ __volatile__ ("" ::: "memory");

    /* 检查错误 */
    uint32_t tfd = ahciRead32(p, PxTFD);
    if ((tfd & (TFD_ERR | TFD_BSY)) || (ahciRead32(p, PxCI) & 1)) {
        serialPutStr("[AHCI] cmd fail tfd=");
        serialPutHex32(tfd);
        serialPutStr(" is=");
        serialPutHex32(ahciRead32(p, PxIS));
        serialPutStr("\n");
        return -1;
    }

    /* 读：回拷数据 */
    if (!write) memcpy(data, dbuf, (size_t)secs * 512);
    return 0;
}

int ahciReadSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, uint8_t* buf) {
    AhciCtx* cx = ahciFindCtx((uintptr_t)drv->abar, drv->port);
    if (!cx) return -1;
    while (cnt > 0) {
        uint32_t n = (cnt > AHCI_MAX_SECTORS) ? AHCI_MAX_SECTORS : cnt;
        if (ahciDoCommand(cx, lba, n, buf, false) != 0) return -1;
        lba += n; cnt -= n; buf += (size_t)n * 512;
    }
    return 0;
}

int ahciWriteSectors(const dDrive* drv, uint32_t lba, uint32_t cnt, const uint8_t* buf) {
    AhciCtx* cx = ahciFindCtx((uintptr_t)drv->abar, drv->port);
    if (!cx) return -1;
    while (cnt > 0) {
        uint32_t n = (cnt > AHCI_MAX_SECTORS) ? AHCI_MAX_SECTORS : cnt;
        if (ahciDoCommand(cx, lba, n, (uint8_t*)buf, true) != 0) return -1;
        lba += n; cnt -= n; buf += (size_t)n * 512;
    }
    return 0;
}

/* ===================== 初始化 ===================== */
int ahciProbe(void) {
    pciDevice pci;
    if (pciFindMassStorage(&pci, PCI_SUBCLASS_SATA, PCI_PROGIF_AHCI) != 0) {
        serialPutStr("[AHCI] no SATA/AHCI controller found\n");
        return 0;
    }

    uint32_t bar5 = pciReadBar32(&pci, PCI_BAR5_OFFSET);
    if (!bar5) {
        serialPutStr("[AHCI] ABAR=0\n");
        return 0;
    }
    gAbar = bar5;

    /* identity 映射 ABAR */
    uint32_t abarSize = pciGetBarSize(&pci, PCI_BAR5_OFFSET);
    for (uint32_t a = gAbar; a < gAbar + abarSize; a += PAGE_SIZE)
        pagingMapPage(a, a, PAGE_PRESENT | PAGE_WRITABLE);

    serialPutStr("[AHCI] ABAR=");
    serialPutHex32(gAbar);
    serialPutStr(" size=");
    serialPutHex32(abarSize);
    serialPutStr("\n");

    /* 复位 + AHCI Enable */
    ahciWrite32(gAbar, GHC, GHC_HR);
    uint32_t t = 0;
    while ((ahciRead32(gAbar, GHC) & GHC_HR) && ++t < 200000) ahciBusyWait(100);
    ahciWrite32(gAbar, GHC, GHC_AE);
    t = 0;
    while (!(ahciRead32(gAbar, GHC) & GHC_AE) && ++t < 200000) ahciBusyWait(100);

    uint32_t pi = ahciRead32(gAbar, PPI);
    int found = 0;
    for (uint8_t port = 0; port < 32; port++) {
        if (!(pi & (1u << port))) continue;

        uintptr_t p = portBase(gAbar, port);
        uint32_t ssts = ahciRead32(p, 0x28);   /* PxSSTS */
        uint8_t devPwr = (uint8_t)((ssts >> 8) & 0x0F);
        if (devPwr != 3) continue;             /* 无设备连接 */

        /* 分配每端口 DMA 资源 */
        AhciCtx* cx = NULL;
        for (int i = 0; i < MAX_DRIVES; i++) {
            if (!gAhciCtx[i].used) { cx = &gAhciCtx[i]; break; }
        }
        if (!cx) break;
        memset(cx, 0, sizeof(*cx));
        cx->clb     = ahciAllocDma(1);
        cx->ctba    = ahciAllocDma(1);
        cx->fb      = ahciAllocDma(1);
        cx->databuf = ahciAllocDma(AHCI_BUF_PAGES);
        if (!cx->clb || !cx->ctba || !cx->fb || !cx->databuf) {
            serialPutStr("[AHCI] dma alloc fail\n");
            continue;
        }
        cx->abar = gAbar;
        cx->port = port;
        cx->used = true;

        if (ahciPortStart(cx) != 0) { cx->used = false; continue; }

        /* 容量:READ_DMA_EXT 无原生 IDENTIFY 容量字段,采用
           一次小读(2 扇区)自证可用,容量保守填 0x1FFFFF(约 4GB)。
           分区扫描/格式会落在实际介质内。 */
        uint8_t ping[1024];
        if (ahciDoCommand(cx, 0, 2, ping, false) != 0) { cx->used = false; continue; }

        /* 挂入磁盘抽象层 */
        dDrive d;
        memset(&d, 0, sizeof(d));
        d.type        = DRIVE_TYPE_AHCI;
        d.present     = true;
        d.capacityLba = 0x1FFFFF;
        d.abar        = gAbar;
        d.port        = port;
        int idx = diskAppendDrive(&d);
        if (idx < 0) { cx->used = false; continue; }

        serialPutStr("[AHCI] drive on port ");
        serialPutHex8(port);
        serialPutStr("\n");
        found++;
    }
    return found;
}