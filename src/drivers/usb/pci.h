#ifndef _DRIVERS_USB_PCI_H
#define _DRIVERS_USB_PCI_H

#include <stdint.h>

#define PCI_CLASS_SERIAL_BUS     0x0C   /* Serial Bus Controller */
#define PCI_SUBCLASS_USB         0x03
#define PCI_PROGIF_XHCI          0x30
#define PCI_PROGIF_EHCI          0x20

#define PCI_CLASS_DISPLAY        0x03   /* Display Controller */

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  func;
    uint16_t vendor;
    uint16_t deviceId;
    uint8_t  baseClass;
    uint8_t  subclass;
    uint8_t  progIf;
    uint32_t bar0;             /* 原始 BAR0 值（内存/IO + 64位高低） */
    uint64_t bar0Addr;         /* 解析后的 BAR0 地址（MMIO） */
    uint32_t bar0Size;         /* BAR0 内存区域大小 */
    uint8_t  irqLine;
} pciDevice;

/* 读取/写入 PCI 配置空间 */
uint32_t pciRead32(const pciDevice* dev, uint8_t offset);
uint16_t pciRead16(const pciDevice* dev, uint8_t offset);
uint8_t  pciRead8(const pciDevice* dev, uint8_t offset);
void     pciWrite32(const pciDevice* dev, uint8_t offset, uint32_t value);

/* 探测总线 0；查找 XHCI，成功返回 0 */
void pciScan(void);
int pciFindController(pciDevice* dev, uint8_t subclass, uint8_t progIf);

/* 探测总线 0；查找指定基础类(如显卡 0x03)设备，成功返回 0，填充 dev->bar0Addr(帧缓冲) */
int pciFindClass(pciDevice* dev, uint8_t baseClass);

#endif