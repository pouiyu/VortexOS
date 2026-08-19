#include "shell.h"
#include "commands.h"
#include <sysinfo/config.h>
#include <vga.h>
#include <keyboard.h>
#include <string/string.h>
#include <kernel.h>
#include <fs/fat32.h>

#define MAX_INPUT 256
#define MAX_HISTORY 32

static char history[MAX_HISTORY][MAX_INPUT];
static int  historyCount = 0;
static int  historyIndex = -1;

static char inputBuf[MAX_INPUT];
static int  inputLen = 0;
static int  cursorPos = 0;
static char currentDir[PATH_MAX] = "/";

static const char* prompt = OS_NAME "> ";
static int promptLen = 0;  // 提示符长度，init 时计算

static int shellRow = 0;  // 提示符所在行

static void historyAdd(const char* cmd) {
    if (cmd[0] == '\0') return;

    if (historyCount < MAX_HISTORY) {
        strcpy(history[historyCount], cmd);
        historyCount++;
    } else {
        // 满了，整体前移
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[MAX_HISTORY - 1], cmd);
    }
}

const char* shellGetCwd(void) {
    return currentDir;
}

void shellSetCwd(const char* path) {
    if (path[0] == '\0') {
        strcpy(currentDir, "/");
    } else {
        strcpy(currentDir, path);
    }
}

static void shellRedrawLine(void) {
    vgaSetCursorPos(shellRow, promptLen);

    for (int i = promptLen; i < VGA_WIDTH - 1; i++)  // 减 1
        vgaPutChar(' ');

    vgaSetCursorPos(shellRow, promptLen);
    for (int i = 0; i < inputLen; i++)
        vgaPutChar(inputBuf[i]);

    vgaSetCursorPos(shellRow, promptLen + cursorPos);
}

static int getPromptLen(void) {
    return strlen(OS_NAME) + 1 + strlen(currentDir) + 2;  // "vortex:" + cwd + "> "
}

static void putPrompt(void) {
    vgaFillLineColor();
    vgaPutStrColor(OS_NAME, HL);
    vgaPutStrColor(":", HL);
    vgaPutStrColor(shellGetCwd(), HL);
    vgaPutStrColor("> ", HL);
    
    promptLen = getPromptLen();
}

static int shellExecute(const char* cmd) {
    if (cmd[0] == '\0') return 0;

    uint8_t row, col;

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

    vgaFillLineColor();
    vgaPutStr(argv[0]);
    vgaPutStr(": command not found\n");
    return 0;
}

void runShell(void) {
    vgaClear();
    vgaEnableCursor();
    vgaSetCursorStyle(14,15);
    vgaPutStrColor(" "OS_NAME"OS\n", HL);
    vgaPutStrColor("Type \"help\" for commands, \"exit\" to return.\n\n", LL);

    uint8_t initRow, initCol;
    vgaGetCursorPos(&initRow, &initCol);
    shellRow = initRow;

    inputLen = 0;
    cursorPos = 0;
    inputBuf[0] = '\0';

    // shellExecute("cat folder2/file6file6file6file6.txt");

    putPrompt();

    for (;;) {
        if (keyboardHasChar()) {
            unsigned char c = keyboardGetChar();

            if (c == '\r' || c == '\n') {
                vgaPutChar('\n');
                inputBuf[inputLen] = '\0';

                historyAdd(inputBuf);
                historyIndex = -1;

                if (shellExecute(inputBuf) == 1)
                    return;

                // 命令执行完，计算提示符所在行
                uint8_t row, col;
                vgaGetCursorPos(&row, &col);
                if (col != 0) {
                    vgaPutChar('\n');
                    row++;
                }
                shellRow = row;

                inputLen = 0;
                cursorPos = 0;
                inputBuf[0] = '\0';
                putPrompt();
            } else if (c == '\b') {
                if (cursorPos > 0 && inputLen > 0) {
                    // 把光标后面的内容往前移
                    for (int i = cursorPos - 1; i < inputLen - 1; i++)
                        inputBuf[i] = inputBuf[i + 1];
                    inputLen--;
                    cursorPos--;
                    shellRedrawLine();
                }
            } else if (c == KEY_UP) {
                if (historyCount > 0) {
                    if (historyIndex < historyCount - 1) {
                        historyIndex++;
                        strcpy(inputBuf, history[historyCount - 1 - historyIndex]);
                        inputLen = strlen(inputBuf);
                        cursorPos = inputLen;
                        shellRedrawLine();
                    }
                }
            } else if (c == KEY_DOWN) {
                if (historyIndex >= 0) {
                    historyIndex--;
                    if (historyIndex < 0) {
                        inputBuf[0] = '\0';
                        inputLen = 0;
                        cursorPos = 0;
                    } else {
                        strcpy(inputBuf, history[historyCount - 1 - historyIndex]);
                        inputLen = strlen(inputBuf);
                        cursorPos = inputLen;
                    }
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
                historyIndex = -1;  // 加这行

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