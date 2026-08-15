#ifndef _KERNEL_SHELL_COMMANDS_H
#define _KERNEL_SHELL_COMMANDS_H

typedef struct {
    const char* name;
    const char* description;
    int (*handler)(int argc, char** argv);
} shellCommand;

const shellCommand* getCommandList(void);

#endif