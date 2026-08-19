#include "commands.h"
#include <sysinfo/config.h>
#include <sysinfo/sysinfo.h>
#include <stdlib/stdlib.h>
#include "device.h"
#include <vga.h>
#include <string/string.h>
#include <fs/fat32.h>
#include <fs/file.h>
#include "shell.h"
#include <rtc.h>
#include <kernel.h>

static int cmdHelp(int argc, char** argv);
static int cmdClear(int argc, char** argv);
static int cmdInfo(int argc, char** argv);
static int cmdDate(int argc, char** argv);
static int cmdTime(int argc, char** argv);
static int cmdLs(int argc, char** argv);
static int cmdCat(int argc, char** argv);
static int cmdCd(int argc, char** argv);
static int cmdPwd(int argc, char** argv);
static int cmdMk(int argc, char** argv);
static int cmdWr(int argc, char** argv);
static int cmdCp(int argc, char** argv);
static int cmdRm(int argc, char** argv);
static int cmdRen(int argc, char** argv);
static int cmdFg(int argc, char** argv);
static int cmdBg(int argc, char** argv);
static int cmdHl(int argc, char** argv);
static int cmdLl(int argc, char** argv);
static int cmdReboot(int argc, char** argv);
static int cmdShutdown(int argc, char** argv);
static int cmdEcho(int argc, char** argv);
static int cmdExit(int argc, char** argv);

static const shellCommand commandList[] = {
    {"help",     "Show available commands",        cmdHelp},
    {"clear",    "Clear the screen",               cmdClear},
    {"info",     "Show system information",        cmdInfo},
    {"date",     "Show current date",              cmdDate},
    {"time",     "Show current time",              cmdTime},
    {"ls",       "Show all files or folders in the current directory", cmdLs},
    {"cat",      "Output the contents of a file",  cmdCat},
    {"cd",       "Change current directory",       cmdCd},
    {"pwd",      "Print current directory",        cmdPwd},
    {"mk",       "Create file or directory (-d to create directory)", cmdMk},
    {"wr",       "Write to file (-a to append)",   cmdWr},
    {"cp",       "Copy file",                      cmdCp},
    {"rm",       "Remove file or directory",       cmdRm},
    {"ren",      "Rename file or directory",       cmdRen},
    {"fg",       "Set foreground color",           cmdFg},
    {"bg",       "Set background color",           cmdBg},
    {"hl",       "Set highlight color",            cmdHl},
    {"ll",       "Set lowlight color",             cmdLl},
    {"reboot",   "Reboot the system",              cmdReboot},
    {"shutdown", "Shutdown the system",            cmdShutdown},
    {"echo",     "Print a line of text",           cmdEcho},
    {"exit",     "Exit to main menu",              cmdExit},
    {NULL, NULL, NULL}
};

const shellCommand* getCommandList(void) {
    return commandList;
}

// 拼接完整路径
static void makeFullPath(const char* input, char* output) {
    if (input[0] == '/') {
        strcpy(output, input);
    } else {
        const char* cwd = shellGetCwd();
        if (strcmp(cwd, "/") == 0) {
            strcpy(output, "/");
            strcat(output, input);
        } else {
            strcpy(output, cwd);
            strcat(output, "/");
            strcat(output, input);
        }
    }
}

// 输出用法提示
static void usage(const char* msg) {
    vgaFillLineColor();
    vgaPutStr(msg);
    vgaPutChar('\n');
}

static int cmdHelp(int argc, char** argv) {
    (void)argc;
    (void)argv;

    vgaFillLineColor();
    vgaPutStr("Available commands:\n");

    for (int i = 0; commandList[i].name; i++) {
        vgaFillLineColor();
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

static int cmdDate(int argc, char** argv) {
    (void)argc;
    (void)argv;

    RtcTime t;
    rtcGetTime(&t);

    vgaFillLineColor();
    putDecimal(t.year);
    vgaPutChar('/');

    if (t.month < 10) vgaPutChar('0');
    putDecimal(t.month);
    vgaPutChar('/');

    if (t.day < 10) vgaPutChar('0');
    putDecimal(t.day);
    vgaPutChar('\n');

    return 0;
}

static int cmdTime(int argc, char** argv) {
    (void)argc;
    (void)argv;

    RtcTime t;
    rtcGetTime(&t);

    vgaFillLineColor();

    if (t.hour < 10) vgaPutChar('0');
    putDecimal(t.hour);
    vgaPutChar(':');

    if (t.minute < 10) vgaPutChar('0');
    putDecimal(t.minute);
    vgaPutChar(':');

    if (t.second < 10) vgaPutChar('0');
    putDecimal(t.second);
    vgaPutChar('\n');

    return 0;
}

static int cmdLs(int argc, char** argv) {
    if (!fsVolume.valid) {
        vgaFillLineColor();
        vgaPutStr("FAT32 not initialized\n");
        return 0;
    }

    char* buf = malloc(PATH_MAX * 4);
    if (!buf) {
        vgaFillLineColor();
        vgaPutStr("Out of memory\n");
        return 0;
    }

    char fullPath[PATH_MAX];
    makeFullPath(argc > 1 ? argv[1] : shellGetCwd(), fullPath);

    if (!fat32ListDir(&fsVolume, fullPath, buf, PATH_MAX * 4)) {
        vgaFillLineColor();
        vgaPutStr("Failed to list directory\n");
        free(buf);
        return 0;
    }

    // 逐行输出
    char* line = buf;
    while (*line) {
        vgaFillLineColor();
        while (*line && *line != '\n') {
            vgaPutChar(*line++);
        }
        if (*line == '\n') line++;
        vgaPutChar('\n');
    }

    free(buf);
    return 0;
}

static int cmdCat(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: cat <filename>");
        return 0;
    }

    char fullPath[PATH_MAX];
    makeFullPath(argv[1], fullPath);

    FileHandle file;
    if (!fsOpen(&file, fullPath)) {
        vgaFillLineColor();
        vgaPutStr("File not found\n");
        return 0;
    }

    char* buf = malloc(512);
    if (!buf) {
        vgaFillLineColor();
        vgaPutStr("Out of memory\n");
        fsClose(&file);
        return 0;
    }

    int n;
    bool lineStart = true;
    while ((n = fsRead(&file, buf, 512)) > 0) {
        for (int i = 0; i < n; i++) {
            if (lineStart) {
                vgaFillLineColor();
                lineStart = false;
            }
            vgaPutChar(buf[i]);
            if (buf[i] == '\n') {
                lineStart = true;
            }
        }
    }

    free(buf);
    fsClose(&file);

    uint8_t row, col;
    vgaGetCursorPos(&row, &col);
    if (col != 0) vgaPutChar('\n');

    return 0;
}
static int cmdCd(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: cd <directory>");
        return 0;
    }

    char fullPath[PATH_MAX];
    makeFullPath(argv[1], fullPath);

    uint32_t cluster;
    if (!fat32PathToCluster(&fsVolume, fullPath, &cluster)) {
        vgaFillLineColor();
        vgaPutStr("Directory not found\n");
        return 0;
    }

    shellSetCwd(fullPath);
    return 0;
}

static int cmdPwd(int argc, char** argv) {
    (void)argc;
    (void)argv;
    vgaFillLineColor();
    vgaPutStr(shellGetCwd());
    vgaPutChar('\n');
    return 0;
}

static int cmdMk(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: mk <filename> or mk -d <dirname>");
        return 0;
    }

    bool isDirectory = false;
    const char* name;

    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            usage("Usage: mk -d <dirname>");
            return 0;
        }
        isDirectory = true;
        name = argv[2];
    } else {
        name = argv[1];
    }

    char fullPath[PATH_MAX];
    makeFullPath(name, fullPath);

    if (fat32CreateEntry(&fsVolume, fullPath, isDirectory)) {
        vgaFillLineColor();
        vgaPutStr(isDirectory ? "Directory created\n" : "File created\n");
    } else {
        vgaFillLineColor();
        vgaPutStr("Create failed\n");
    }

    return 0;
}

static int cmdWr(int argc, char** argv) {
    if (argc < 3) {
        usage("Usage: wr <file> \"content\" or wr -a <file> \"content\"");
        return 0;
    }

    bool append = false;
    const char* filename;
    const char* content;

    if (strcmp(argv[1], "-a") == 0) {
        if (argc < 4) {
            usage("Usage: wr -a <file> \"content\"");
            return 0;
        }
        append = true;
        filename = argv[2];
        content = argv[3];
    } else {
        filename = argv[1];
        content = argv[2];
    }

    char cleanContent[512];
    int len = strlen(content);
    if (len >= 2 && content[0] == '"' && content[len - 1] == '"') {
        memcpy(cleanContent, content + 1, len - 2);
        cleanContent[len - 2] = '\0';
    } else {
        strcpy(cleanContent, content);
    }

    for (int i = append ? 4 : 3; i < argc; i++) {
        strcat(cleanContent, " ");
        strcat(cleanContent, argv[i]);
        int clen = strlen(cleanContent);
        if (clen > 0 && cleanContent[clen - 1] == '"') {
            cleanContent[clen - 1] = '\0';
        }
    }

    char fullPath[PATH_MAX];
    makeFullPath(filename, fullPath);

    if (fat32WriteFile(&fsVolume, fullPath, cleanContent, append)) {
        vgaFillLineColor();
        vgaPutStr("Written\n");
    } else {
        vgaFillLineColor();
        vgaPutStr("Write failed\n");
    }

    return 0;
}

static int cmdCp(int argc, char** argv) {
    if (argc < 3) {
        usage("Usage: cp <src> <dst>");
        return 0;
    }

    char srcPath[PATH_MAX];
    char dstPath[PATH_MAX];
    makeFullPath(argv[1], srcPath);
    makeFullPath(argv[2], dstPath);

    if (fat32CopyFile(&fsVolume, srcPath, dstPath)) {
        vgaFillLineColor();
        vgaPutStr("Copied\n");
    } else {
        vgaFillLineColor();
        vgaPutStr("Copy failed\n");
    }

    return 0;
}

static int cmdRm(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: rm <file or directory>");
        return 0;
    }

    char fullPath[PATH_MAX];
    makeFullPath(argv[1], fullPath);

    if (fat32Remove(&fsVolume, fullPath)) {
        vgaFillLineColor();
        vgaPutStr("Removed\n");
    } else {
        vgaFillLineColor();
        vgaPutStr("Remove failed\n");
    }
    return 0;
}

static int cmdRen(int argc, char** argv) {
    if (argc < 3) {
        usage("Usage: ren <old> <new>");
        return 0;
    }

    char fullPath[PATH_MAX];
    makeFullPath(argv[1], fullPath);

    if (fat32Rename(&fsVolume, fullPath, argv[2])) {
        vgaFillLineColor();
        vgaPutStr("Renamed\n");
    } else {
        vgaFillLineColor();
        vgaPutStr("Rename failed\n");
    }
    return 0;
}

// 颜色名转数字
static int colorNameToIndex(const char* name) {
    static const char* colorNames[] = {
        "black", "blue", "green", "cyan", "red", "magenta",
        "brown", "light_grey", "dark_grey", "light_blue",
        "light_green", "light_cyan", "light_red", "light_magenta",
        "yellow", "white"
    };

    for (int i = 0; i < 16; i++) {
        if (strcasecmp(name, colorNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

// 解析颜色参数
static int parseColor(const char* arg) {
    if (arg[0] >= '0' && arg[0] <= '9') {
        int val = 0;
        for (int i = 0; arg[i]; i++) {
            val = val * 10 + (arg[i] - '0');
        }
        if (val >= 0 && val <= 15) return val;
        return -1;
    }
    return colorNameToIndex(arg);
}

static int cmdFg(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: fg <color>");
        return 0;
    }

    int color = parseColor(argv[1]);
    if (color < 0 || color > 15) {
        vgaFillLineColor();
        vgaPutStr("Invalid color\n");
        return 0;
    }

    if (color == BG) {
        vgaFillLineColor();
        vgaPutStr("FG cannot equal BG\n");
        return 0;
    }

    FG = color;
    updateTheme();
    return 0;
}

static int cmdBg(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: bg <color>");
        return 0;
    }

    int color = parseColor(argv[1]);
    if (color < 0 || color > 15) {
        vgaFillLineColor();
        vgaPutStr("Invalid color\n");
        return 0;
    }

    if (color == FG) {
        vgaFillLineColor();
        vgaPutStr("BG cannot equal FG\n");
        return 0;
    }

    BG = color;
    HL = VGA_COLOR(HL & 0x0F, BG);
    LL = VGA_COLOR(LL & 0x0F, BG);
    updateTheme();
    return 0;
}

static int cmdHl(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: hl <color>");
        return 0;
    }

    int color = parseColor(argv[1]);
    if (color < 0 || color > 15) {
        vgaFillLineColor();
        vgaPutStr("Invalid color\n");
        return 0;
    }

    HL = VGA_COLOR(color, BG);
    return 0;
}

static int cmdLl(int argc, char** argv) {
    if (argc < 2) {
        usage("Usage: ll <color>");
        return 0;
    }

    int color = parseColor(argv[1]);
    if (color < 0 || color > 15) {
        vgaFillLineColor();
        vgaPutStr("Invalid color\n");
        return 0;
    }

    LL = VGA_COLOR(color, BG);
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
    vgaFillLineColor();
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