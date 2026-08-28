#ifndef _DRIVERS_USB_HID_H
#define _DRIVERS_USB_HID_H

/* 装载由 XHCI 枚举出的 HID 设备：注册上报回调并启动中断 IN 传输。
 * 键盘上报会换算为 PS/2 扫描码注入现有输入管线（保留 PS/2 作为回退）。
 * 鼠标上报当前仅打印，供后续接入光标。 */
int hidLoadFromXhci(void);

#endif