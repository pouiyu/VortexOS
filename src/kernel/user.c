#include "user.h"
#include "syscall.h"
#include <stdint.h>

void jumpToUserMode(void* entry) {
    uint32_t eip = (uint32_t)entry;

    __asm__ volatile (
        "cli\n"
        "pushl $0x23\n"
        "pushl $0x300000\n"
        "pushl $0x202\n"
        "pushl $0x1B\n"
        "pushl %0\n"
        "iret\n"
        :
        : "m"(eip)
        : "ax"
    );
}

uint32_t sysRead(char* buf) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_READ), "b"(buf)
    );
    return ret;
}

uint32_t sysWrite(const char* str) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(str)
    );
    return ret;
}

void sysExit(void) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_EXIT)
    );
}

void userTest(void) {
    sysWrite("Hello from user mode!\n");
    sysWrite("Press any key: ");

    char c;
    sysRead(&c);

    sysWrite("You pressed: ");
    char msg[2] = {c, '\n'};
    sysWrite(msg);

    sysExit();
}

/* ---- 用户 GUI 程序进入/退出(不经任务调度器) ---- */

static uint32_t gGuiKernelEsp = 0;   /* jumpToUserGui 时的内核栈 */
static uint32_t gGuiKernelRet = 0;   /* 用户程序退出后的内核续点(onExit) */

void jumpToUserGui(void* entry, void (*onExit)(void)) {
    gGuiKernelRet = (uint32_t)onExit;

    __asm__ volatile (
        "cli\n"
        "movl %%esp, %0\n"          /* 保存内核栈 */
        "pushl $0x23\n"             /* ss = 用户数据段 */
        "pushl $0x300000\n"         /* esp = 用户栈 */
        "pushl $0x202\n"            /* eflags: IF=1 */
        "pushl $0x1B\n"             /* cs = 用户代码段 */
        "pushl %1\n"                /* eip = 程序入口 */
        "iret\n"
        : "=m"(gGuiKernelEsp)
        : "r"(entry)
        : "memory", "eax"
    );

    for (;;);   /* 不会到达 */
}

__attribute__((noreturn))
void sysExitKernel(void) {
    if (!gGuiKernelRet) {
        /* 没有 GUI 上下文(普通用户程序退出)：维持旧的停机行为 */
        __asm__ volatile ("cli; hlt");
        for (;;);
    }

    /* 同特权级 iret 回内核：栈顶依次为 eip/cs/eflags。
     * iret 不弹 ss/esp，esp 保持为恢复的内核栈。 */
    __asm__ volatile (
        "movl %0, %%esp\n"
        "pushl $0x202\n"            /* eflags: IF=1 */
        "pushl $0x08\n"             /* cs = 内核代码段 */
        "pushl %1\n"                /* eip = onExit */
        "iret\n"
        :
        : "r"(gGuiKernelEsp), "r"(gGuiKernelRet)
        : "memory"
    );

    for (;;);   /* 不会到达 */
}