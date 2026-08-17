#include "commands.h"
#include <sysinfo/config.h>
#include <sysinfo/sysinfo.h>
#include "device.h"
#include <vga.h>
#include <string.h>
#include <fs/fat32.h>
#include <fs/file.h>
#include "shell.h"

static int cmdHelp(int argc, char** argv);
static int cmdClear(int argc, char** argv);
static int cmdInfo(int argc, char** argv);
static int cmdLs(int argc, char** argv);
static int cmdCat(int argc, char** argv);
static int cmdCd(int argc, char** argv);
static int cmdReboot(int argc, char** argv);
static int cmdShutdown(int argc, char** argv);
static int cmdEcho(int argc, char** argv);
static int cmdExit(int argc, char** argv);

static const shellCommand commandList[] = {
    {"help",     "Show available commands",        cmdHelp},
    {"clear",    "Clear the screen",               cmdClear},
    {"info",     "Show system information",        cmdInfo},
    {"ls",       "Show all files or folders in the current directory",        cmdLs},
    {"cat",      "Output the contents of a file",  cmdCat},
    {"cd", "Change current directory", cmdCd},
    {"reboot",   "Reboot the system",              cmdReboot},
    {"shutdown", "Shutdown the system",            cmdShutdown},
    {"echo",     "Print a line of text",           cmdEcho},
    {"exit",     "Exit to main menu",           cmdExit},
    {NULL, NULL, NULL}
};

const shellCommand* getCommandList(void) {
    return commandList;
}

static int cmdHelp(int argc, char** argv) {
    (void)argc;
    (void)argv;
    vgaPutStr("Available commands:\n");
    for (int i = 0; commandList[i].name; i++) {
        vgaPutStr("  ");
        vgaPutStr(commandList[i].name);
        vgaPutStr(" - ");
        vgaPutStr(commandList[i].description);
        vgaPutChar('\n');
    }
    return 0;
}

static int cmdClear(int argc, char** argv) {
    (void)argc;
    (void)argv;
    vgaClear();
    return 0;
}

static int cmdInfo(int argc, char** argv) {
    (void)argc;
    (void)argv;
    showSystemInfo();
    vgaClear();
    return 0;
}

static int cmdLs(int argc, char** argv) {
    char buf[2048];

    if (!fsVolume.valid) {
        vgaPutStr("FAT32 not initialized\n");
        return 0;
    }

    const char* path = shellGetCwd();
    if (argc > 1) {
        if (argv[1][0] == '/') {
            path = argv[1];
        } else {
            // 相对路径拼接
            static char fullPath[PATH_MAX];
            const char* cwd = shellGetCwd();
            if (strcmp(cwd, "/") == 0) {
                strcpy(fullPath, "/");
            } else {
                strcpy(fullPath, cwd);
                strcat(fullPath, "/");
            }
            strcat(fullPath, argv[1]);
            path = fullPath;
        }
    }

    if (!fat32ListDir(&fsVolume, path, buf, sizeof(buf))) {
        vgaPutStr("Failed to list directory\n");
        return 0;
    }

    vgaPutStr(buf);
    return 0;
}

static int cmdCat(int argc, char** argv) {
    if (argc < 2) {
        vgaPutStr("Usage: cat <filename>\n");
        return 0;
    }

    char fullPath[PATH_MAX];
    if (argv[1][0] == '/') {
        strcpy(fullPath, argv[1]);
    } else {
        const char* cwd = shellGetCwd();
        if (strcmp(cwd, "/") == 0) {
            strcpy(fullPath, "/");
        } else {
            strcpy(fullPath, cwd);
            strcat(fullPath, "/");
        }
        strcat(fullPath, argv[1]);
    }

    FileHandle file;
    if (!fsOpen(&file, fullPath)) {
        vgaPutStr("File not found\n");
        return 0;
    }

    char buf[512];
    int n;
    while ((n = fsRead(&file, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            vgaPutChar(buf[i]);
        }
    }

    fsClose(&file);

    // 只有当光标不在行首时才补换行
    uint8_t row, col;
    vgaGetCursorPos(&row, &col);
    if (col != 0) {
        vgaPutChar('\n');
    }
    return 0;
}

static int cmdCd(int argc, char** argv) {
    if (argc < 2) {
        vgaPutStr("Usage: cd <directory>\n");
        return 0;
    }

    const char* target = argv[1];

    // 如果是绝对路径
    if (target[0] == '/') {
        // 验证目录存在
        uint32_t cluster;
        if (!fat32PathToCluster(&fsVolume, target, &cluster)) {
            vgaPutStr("Directory not found\n");
            return 0;
        }
        shellSetCwd(target);
    } else {
        // 相对路径：拼接
        char newPath[PATH_MAX];
        const char* cwd = shellGetCwd();

        if (strcmp(cwd, "/") == 0) {
            strcpy(newPath, "/");
            strcat(newPath, target);
        } else {
            strcpy(newPath, cwd);
            strcat(newPath, "/");
            strcat(newPath, target);
        }

        uint32_t cluster;
        if (!fat32PathToCluster(&fsVolume, newPath, &cluster)) {
            vgaPutStr("Directory not found\n");
            return 0;
        }
        shellSetCwd(newPath);
    }

    return 0;
}

static int cmdReboot(int argc, char** argv) {
    (void)argc;
    (void)argv;
    deviceReboot();
    return 0;
}

static int cmdShutdown(int argc, char** argv) {
    (void)argc;
    (void)argv;
    deviceShutdown();
    return 0;
}

static int cmdEcho(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vgaPutChar(' ');
        vgaPutStr(argv[i]);
    }
    vgaPutChar('\n');
    return 0;
}

static int cmdExit(int argc, char** argv) {
    (void)argc;
    (void)argv;
    vgaDisableCursor();
    return 1;
}