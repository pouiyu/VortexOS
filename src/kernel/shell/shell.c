#include "shell.h"
#include "commands.h"
#include <sysinfo/config.h>
#include <vga.h>
#include <keyboard.h>
#include <string.h>
#include <kernel.h>

#define MAX_INPUT 256

static char inputBuf[MAX_INPUT];
static int  inputLen = 0;

static const char* prompt = OS_NAME "> ";

// shellExecute 返回 int，1 表示退出
static int shellExecute(const char* cmd) {
    if (cmd[0] == '\0') return 0;

    char* argv[16];
    int argc = 0;
    char buf[MAX_INPUT];
    strcpy(buf, cmd);

    char* token = strtok(buf, " ");
    while (token && argc < 16) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    const shellCommand* cmds = getCommandList();
    for (int i = 0; cmds[i].name; i++) {
        if (strcmp(argv[0], cmds[i].name) == 0) {
            int ret=cmds[i].handler(argc, argv);
            if (ret) {
                vgaClear();
                drawMainMenu();
                return 1;
            }else{
                return 0;
            }
        }
    }

    vgaPutStr(argv[0]);
    vgaPutStr(": command not found\n");
    return 0;
}

void runShell(void) {
    vgaClear();
    vgaPutStr(OS_NAME " Shell\nType \"help\" for commands, \"exit\" to return.\n\n");

    inputLen = 0;
    inputBuf[0] = '\0';
    vgaPutStr(prompt);

    for (;;) {
        if (keyboardHasChar()) {
            char c = keyboardGetChar();

            if (c == '\r' || c == '\n') {
                vgaPutChar('\n');
                inputBuf[inputLen] = '\0';

                if (shellExecute(inputBuf) == 1) {
                    return;
                }

                inputLen = 0;
                inputBuf[0] = '\0';
                vgaPutStr(prompt);
            } else if (c == '\b') {
                if (inputLen > 0) {
                    inputLen--;
                    uint8_t row, col;
                    vgaGetCursorPos(&row, &col);
                    if (col > 0) {
                        col--;
                        vgaSetCursorPos(row, col);
                        vgaPutChar(' ');
                        vgaSetCursorPos(row, col);
                    }
                }
            } else if (c >= ' ' && c <= '~' && inputLen < MAX_INPUT - 1) {
                inputBuf[inputLen++] = c;
                vgaPutChar(c);
            }
        }
        __asm__ volatile ("hlt");
    }
}