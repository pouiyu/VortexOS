#ifndef _KERNEL_SHELL_SHELL_H
#define _KERNEL_SHELL_SHELL_H

void runShell(void);
const char* shellGetCwd(void);
void shellSetCwd(const char* path);

#endif