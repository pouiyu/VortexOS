#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <stdint.h>
#include <wm/guiapi.h>   /* 系统调用号(SYS_READ/WRITE/EXIT、SYS_WM_*)与 ABI 结构 */

void syscallInit(void);

/* C 分发器：由 syscall_entry(asm) 对非 1/2/3 的系统调用转调，
 * 参数对应 EAX=num, EBX=a, ECX=b, EDX=c，返回值为 syscall 的 EAX 结果。 */
int syscallDispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c);

#endif
