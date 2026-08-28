#include "pci.h"
#include <stdbool.h>
#include <io.h>
#include <serial.h>

/* x86 经典 PCI 配置窗口（配置地址端口 0xCF8 / 0xCFC） */
#define PCI_CONFIG_ADDR   0xCF8
#define PCI_CONFIG_DATA   0xCFC

#define PCI_ENABLE_BIT    0x80000000

/* 扩展配置/F0 掩码由配置地址的低 8 位决定 */
static uint32_t pciMakeAddr(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    return PCI_ENABLE_BIT
           | ((uint32_t)bus   << 16)
           | ((uint32_t)device << 11)
           | ((uint32_t)func   << 8)
           | (offset & 0xFC);
}

uint32_t pciRead32(const pciDevice* dev, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pciMakeAddr(dev->bus, dev->device, dev->func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pciRead16(const pciDevice* dev, uint8_t offset) {
    return (uint16_t)(pciRead32(dev, offset) >> ((offset & 2) * 8));
}

uint8_t pciRead8(const pciDevice* dev, uint8_t offset) {
    return (uint8_t)(pciRead32(dev, offset) >> ((offset & 3) * 8));
}

void pciWrite32(const pciDevice* dev, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pciMakeAddr(dev->bus, dev->device, dev->func, offset));
    outl(PCI_CONFIG_DATA, value);
}

/* 计算 BARx 的内存区域大小（写入全 1 再读回） */
static uint32_t pciGetBarSize(const pciDevice* dev, uint8_t barOffset) {
    uint32_t old = pciRead32(dev, barOffset);
    pciWrite32(dev, barOffset, 0xFFFFFFFF);
    uint32_t size = pciRead32(dev, barOffset);
    pciWrite32(dev, barOffset, old);
    /* 屏蔽类型位/预取位，取 2 的幂 */
    size &= ~0xF;
    size = ~size + 1;
    return size;
}

void pciScan(void) {
    (void)0;
}

/* 填充 dev 的标识字段与 BAR0(内存帧缓冲) */
static void pciFillDevice(pciDevice* dev, uint8_t device, uint8_t func,
                          uint8_t baseClass, uint8_t subClass, uint8_t prog,
                          uint32_t vendor, uint32_t header) {
    dev->bus      = 0;
    dev->device   = device;
    dev->func     = func;
    dev->vendor   = (uint16_t)vendor;
    dev->deviceId = (uint16_t)((header >> 16) & 0xFFFF);
    dev->baseClass = baseClass;
    dev->subclass  = subClass;
    dev->progIf    = prog;

    uint32_t bar0 = pciRead32(dev, 0x10);
    dev->bar0 = bar0;
    uint32_t bar1 = pciRead32(dev, 0x14);

    /* 解析 BAR0：64 位内存 BAR 或 32 位 */
    if (bar0 & 0x01) {
        /* IO BAR，不适用，忽略 */
        dev->bar0Addr = 0;
        dev->bar0Size = 0;
    } else {
        uint64_t addr = bar0 & ~0xF;
        if ((bar0 & 0x06) == 0x04) {   /* 64-bit */
            addr |= ((uint64_t)bar1 << 32);
            dev->bar0Addr = addr;
            dev->bar0Size = pciGetBarSize(dev, 0x10) + pciGetBarSize(dev, 0x14);
        } else {
            dev->bar0Addr = addr;
            dev->bar0Size = pciGetBarSize(dev, 0x10);
        }
    }

    dev->irqLine = pciRead8(dev, 0x3C);
}

static int pciScanBus(pciDevice* dev, uint8_t wantClass, uint8_t wantSubclass,
                      uint8_t wantProgIf, bool byClassOnly) {
    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t func = 0; func < 8; func++) {
            pciDevice probe;
            probe.bus = 0;
            probe.device = device;
            probe.func = func;

            uint32_t header = pciRead32(&probe, 0x00);  /* VendorID + DeviceID */
            uint16_t vendor = (uint16_t)(header & 0xFFFF);
            if (vendor == 0xFFFF || vendor == 0x0000) {
                if (func == 0) break;   /* 该设备不存在，跳过其余 func */
                continue;
            }

            uint32_t revClass = pciRead32(&probe, 0x08);
            uint8_t baseClass = (uint8_t)((revClass >> 24) & 0xFF);
            uint8_t subClass = (uint8_t)((revClass >> 16) & 0xFF);
            uint8_t prog = (uint8_t)((revClass >> 8) & 0xFF);

            bool match = byClassOnly ? (baseClass == wantClass)
                                     : (baseClass == wantClass && subClass == wantSubclass
                                        && (wantProgIf == 0xFF || prog == wantProgIf));
            if (match) {
                pciFillDevice(dev, device, func, baseClass, subClass, prog,
                              vendor, header);
                return 0;
            }
        }
    }
    return -1;
}

int pciFindController(pciDevice* dev, uint8_t subclass, uint8_t progIf) {
    return pciScanBus(dev, PCI_CLASS_SERIAL_BUS, subclass, progIf, false);
}

int pciFindClass(pciDevice* dev, uint8_t baseClass) {
    return pciScanBus(dev, baseClass, 0, 0xFF, true);
}