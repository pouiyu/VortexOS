#ifndef _DRIVERS_USB_XHCI_H
#define _DRIVERS_USB_XHCI_H

#include <stdint.h>
#include <stdbool.h>

/* HID 上报回调：USB 设备中断 IN 端点收到数据的入口 */
typedef void (*XhciReportHandler)(uint8_t slotId, uint8_t* data, uint32_t len);

/* 初始化：探测 PCI + 初始化控制器 + 枚举并配置已连接的 HID 设备 */
int xhciInit(void);

/* 供中断使用：XHCI IRQ 处理器（由 isr 调用） */
void xhciIRQHandler(void);

/* HID 层：为指定 slot 注册上报回调并启动第一个中断 IN 传输 */
int xhciRegisterEp1Handler(uint8_t slotId, XhciReportHandler handler);
int xhciArmInterruptIn(uint8_t slotId);

/* HID 层在枚举后查询 XHCI 已枚举的设备（用于区分键盘/鼠标） */
typedef struct {
    uint8_t slotId;
    uint8_t deviceClass;
    uint8_t deviceSubclass;
    uint8_t deviceProtocol;
} XhciDeviceInfo;

int xhciGetDeviceCount(void);
int xhciGetDeviceInfo(uint8_t index, XhciDeviceInfo* info);

#endif