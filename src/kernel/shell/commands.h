#ifndef _KERNEL_SHELL_COMMANDS_H
#define _KERNEL_SHELL_COMMANDS_H

typedef struct {
    const char* name;
    const char* description;
    int (*handler)(int argc, char** argv);
} shellCommand;

const shellCommand* getCommandList(void);

static int cmdHelp(int argc, char** argv);
static int cmdClear(int argc, char** argv);
static int cmdInfo(int argc, char** argv);
static int cmdReboot(int argc, char** argv);
static int cmdShutdown(int argc, char** argv);
static int cmdEcho(int argc, char** argv);
static int cmdExit(int argc, char** argv);

#endif