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