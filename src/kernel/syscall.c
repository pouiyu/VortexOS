#include "syscall.h"
#include "interruption/idt.h"
#include <vga.h>
#include <keyboard.h>

void syscallHandler(uint32_t num, uint32_t arg1, uint32_t arg2) {
    (void)arg2;

    switch (num) {
        case SYS_WRITE:
            vgaPutStr((const char*)arg1);
            break;

        case SYS_READ: {
            while (!keyboardHasChar()) {
                __asm__ volatile ("hlt");
            }
            unsigned char c = keyboardGetChar();
            char* buf = (char*)arg1;
            if (buf) {
                buf[0] = c;
            }
            break;
        }

        case SYS_EXIT:
            vgaClear();
            vgaPutStr("Program exited.\n");
            __asm__ volatile ("cli; hlt");
            for (;;);
    }
}

void syscallInit(void) {
    extern void syscall_entry(void);
    idtSetGate(0x80, syscall_entry, 0x08, 0xEE);
}