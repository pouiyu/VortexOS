#include "commands.h"
#include <sysinfo/config.h>
#include <sysinfo/sysinfo.h>
#include "device.h"
#include <vga.h>
#include <string.h>
#include <fs/fat32.h>

static int cmdHelp(int argc, char** argv);
static int cmdClear(int argc, char** argv);
static int cmdInfo(int argc, char** argv);
static int cmdLs(int argc, char** argv);
static int cmdReboot(int argc, char** argv);
static int cmdShutdown(int argc, char** argv);
static int cmdEcho(int argc, char** argv);
static int cmdExit(int argc, char** argv);

static const shellCommand commandList[] = {
    {"help",     "Show available commands",        cmdHelp},
    {"clear",    "Clear the screen",               cmdClear},
    {"info",     "Show system information",        cmdInfo},
    {"ls",       "Show all files or folders in the current directory",        cmdLs},
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
    (void)argc;
    (void)argv;

    char buf[2048];
    if (!fsVolume.valid) {
        vgaPutStr("FAT32 not initialized\n");
        return 0;
    }

    if (!fat32ListDir(&fsVolume, "/", buf, sizeof(buf))) {
        vgaPutStr("Failed to list directory\n");
        return 0;
    }

    vgaPutStr(buf);
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