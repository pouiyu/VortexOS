#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#define SYS_READ   1
#define SYS_WRITE  2
#define SYS_EXIT   3

void syscallInit(void);

#endif