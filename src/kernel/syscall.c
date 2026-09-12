#include "syscall.h"
#include "interruption/idt.h"
#include <wm/wmsvc.h>

/* 除 read/write/exit(在 asm 入口内联处理)外的系统调用统一在此分发。
 * 当前仅接入 WM 服务组；未知调用号返回 -1。 */
int syscallDispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    if (num >= SYS_WM_CREATE && num <= SYS_WM_GET_POS)
        return wmsvcHandle(num, a, b, c);
    return -1;
}

void syscallInit(void) {
    extern void syscall_entry(void);
    idtSetGate(0x80, syscall_entry, 0x08, 0xEE);
}
