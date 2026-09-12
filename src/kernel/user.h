#ifndef _KERNEL_USER_H
#define _KERNEL_USER_H

#include <stdint.h>

void jumpToUserMode(void* entry);
uint32_t sysRead(char* buf);
uint32_t sysWrite(const char* str);
void sysExit(void);
void userTest(void);

/* 从内核(CPL0)直接进入用户 GUI 程序(CPL3)：保存内核栈与退出续点，
 * 经 iret 切换到用户态。用户程序调用 SYS_EXIT 时由 sysExitKernel 恢复
 * 到此续点(onExit)继续执行。entry 为用户程序入口，onExit 为 noreturn 的
 * 内核侧退出回调(通常恢复文本模式并回到主菜单)。 */
void jumpToUserGui(void* entry, void (*onExit)(void));

/* SYS_EXIT 内核侧处理：恢复 jumpToUserGui 保存的内核上下文。 */
void sysExitKernel(void);

#endif