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
static int  cursorPos = 0;

static const char* prompt = OS_NAME "> ";
static int promptLen = 0;  // 提示符长度，init 时计算

static void shellRedrawLine(void) {
    uint8_t row, col;
    vgaGetCursorPos(&row, &col);

    // 回到行首（提示符末尾）
    vgaSetCursorPos(row, promptLen);

    // 清空本行剩余
    for (int i = promptLen; i < VGA_WIDTH; i++)
        vgaPutChar(' ');

    // 重新输出输入内容
    vgaSetCursorPos(row, promptLen);
    for (int i = 0; i < inputLen; i++)
        vgaPutChar(inputBuf[i]);

    // 光标移到正确位置
    vgaSetCursorPos(row, promptLen + cursorPos);
}

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
            int ret = cmds[i].handler(argc, argv);
            if (ret) {
                vgaClear();
                drawMainMenu();
                return 1;
            }
            return 0;
        }
    }

    vgaPutStr(argv[0]);
    vgaPutStr(": command not found\n");
    return 0;
}

void runShell(void) {
    vgaClear();
    vgaPutStrColor(OS_NAME " Shell\n", HL);
    vgaPutStrColor("Type \"help\" for commands, \"exit\" to return.\n\n", LL);

    promptLen = strlen(prompt);
    inputLen = 0;
    cursorPos = 0;
    inputBuf[0] = '\0';
    vgaPutStrColor(prompt, HL);

    for (;;) {
        if (keyboardHasChar()) {
            char c = keyboardGetChar();

            if (c == '\r' || c == '\n') {
                vgaPutChar('\n');
                inputBuf[inputLen] = '\0';

                if (shellExecute(inputBuf) == 1)
                    return;

                inputLen = 0;
                cursorPos = 0;
                inputBuf[0] = '\0';
                vgaPutStrColor(prompt, HL);
            } else if (c == '\b') {
                if (cursorPos > 0 && inputLen > 0) {
                    // 把光标后面的内容往前移
                    for (int i = cursorPos - 1; i < inputLen - 1; i++)
                        inputBuf[i] = inputBuf[i + 1];
                    inputLen--;
                    cursorPos--;
                    shellRedrawLine();
                }
            } else if (c == KEY_LEFT) {
                if (cursorPos > 0) {
                    cursorPos--;
                    uint8_t row, col;
                    vgaGetCursorPos(&row, &col);
                    if (col > 0) col--;
                    vgaSetCursorPos(row, col);
                }
            } else if (c == KEY_RIGHT) {
                if (cursorPos < inputLen) {
                    cursorPos++;
                    uint8_t row, col;
                    vgaGetCursorPos(&row, &col);
                    if (col < VGA_WIDTH - 1) col++;
                    vgaSetCursorPos(row, col);
                }
            } else if (c >= ' ' && c <= '~' && inputLen < MAX_INPUT - 1) {
                // 在 cursorPos 处插入字符
                for (int i = inputLen; i > cursorPos; i--)
                    inputBuf[i] = inputBuf[i - 1];
                inputBuf[cursorPos] = c;
                inputLen++;
                cursorPos++;
                shellRedrawLine();
            }
        }
        __asm__ volatile ("hlt");
    }
}