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
static int cmdPwd(int argc, char** argv);
static int cmdMk(int argc, char** argv);
static int cmdWr(int argc, char** argv);
static int cmdCp(int argc, char** argv);
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
    {"pwd", "Print current directory", cmdPwd},
    {"mk", "Create file or directory (-f to create directory)", cmdMk},
    {"wr", "Write to file (-a to append)", cmdWr},
    {"cp", "Copy file", cmdCp},
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

static int cmdPwd(int argc, char** argv) {
    (void)argc;
    (void)argv;
    vgaPutStr(shellGetCwd());
    vgaPutChar('\n');
    return 0;
}

static int cmdMk(int argc, char** argv) {
    if (argc < 2) {
        vgaPutStr("Usage: mk <filename> or mk -d <dirname>\n");
        return 0;
    }

    bool isDirectory = false;
    const char* name;

    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            vgaPutStr("Usage: mk -d <dirname>\n");
            return 0;
        }
        isDirectory = true;
        name = argv[2];
    } else {
        isDirectory = false;
        name = argv[1];
    }

    char fullPath[PATH_MAX];
    const char* cwd = shellGetCwd();

    if (name[0] == '/') {
        strcpy(fullPath, name);
    } else {
        if (strcmp(cwd, "/") == 0) {
            strcpy(fullPath, "/");
            strcat(fullPath, name);
        } else {
            strcpy(fullPath, cwd);
            strcat(fullPath, "/");
            strcat(fullPath, name);
        }
    }

    if (fat32CreateEntry(&fsVolume, fullPath, isDirectory)) {
        vgaPutStr(isDirectory ? "Directory created\n" : "File created\n");
    } else {
        vgaPutStr("Create failed\n");
    }

    return 0;
}

static int cmdWr(int argc, char** argv) {
    if (argc < 3) {
        vgaPutStr("Usage: wr <file> \"content\" or wr -a <file> \"content\"\n");
        return 0;
    }

    bool append = false;
    const char* filename;
    const char* content;

    if (strcmp(argv[1], "-a") == 0) {
        if (argc < 4) {
            vgaPutStr("Usage: wr -a <file> \"content\"\n");
            return 0;
        }
        append = true;
        filename = argv[2];
        content = argv[3];
    } else {
        filename = argv[1];
        content = argv[2];
    }

    // 去掉双引号
    char cleanContent[512];
    int len = strlen(content);
    if (len >= 2 && content[0] == '"' && content[len - 1] == '"') {
        memcpy(cleanContent, content + 1, len - 2);
        cleanContent[len - 2] = '\0';
    } else {
        strcpy(cleanContent, content);
    }

    // 合并多个参数
    for (int i = append ? 4 : 3; i < argc; i++) {
        strcat(cleanContent, " ");
        strcat(cleanContent, argv[i]);
        // 如果最后有引号，去掉
        int clen = strlen(cleanContent);
        if (clen > 0 && cleanContent[clen - 1] == '"') {
            cleanContent[clen - 1] = '\0';
        }
    }

    // 拼接完整路径
    char fullPath[PATH_MAX];
    const char* cwd = shellGetCwd();
    if (filename[0] == '/') {
        strcpy(fullPath, filename);
    } else {
        if (strcmp(cwd, "/") == 0) {
            strcpy(fullPath, "/");
            strcat(fullPath, filename);
        } else {
            strcpy(fullPath, cwd);
            strcat(fullPath, "/");
            strcat(fullPath, filename);
        }
    }

    if (fat32WriteFile(&fsVolume, fullPath, cleanContent, append)) {
        vgaPutStr("Written\n");
    } else {
        vgaPutStr("Write failed\n");
    }

    return 0;
}

static int cmdCp(int argc, char** argv) {
    if (argc < 3) {
        vgaPutStr("Usage: cp <src> <dst>\n");
        return 0;
    }

    // 拼接源路径
    char srcPath[PATH_MAX];
    const char* cwd = shellGetCwd();
    if (argv[1][0] == '/') {
        strcpy(srcPath, argv[1]);
    } else {
        if (strcmp(cwd, "/") == 0) {
            strcpy(srcPath, "/");
            strcat(srcPath, argv[1]);
        } else {
            strcpy(srcPath, cwd);
            strcat(srcPath, "/");
            strcat(srcPath, argv[1]);
        }
    }

    // 拼接目标路径
    char dstPath[PATH_MAX];
    if (argv[2][0] == '/') {
        strcpy(dstPath, argv[2]);
    } else {
        if (strcmp(cwd, "/") == 0) {
            strcpy(dstPath, "/");
            strcat(dstPath, argv[2]);
        } else {
            strcpy(dstPath, cwd);
            strcat(dstPath, "/");
            strcat(dstPath, argv[2]);
        }
    }

    if (fat32CopyFile(&fsVolume, srcPath, dstPath)) {
        vgaPutStr("Copied\n");
    } else {
        vgaPutStr("Copy failed\n");
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