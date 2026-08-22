#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>

#define MAX_TASKS 16

typedef struct {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
} TaskContext;

typedef struct {
    TaskContext ctx;
    uint32_t* stack;
    uint32_t* userStack;
    uint32_t stackSize;
    int state;
    int id;
    int isUser;
} Task;

extern Task* currentTask;
extern Task* nextTask;

void taskInit(void);
Task* taskCreate(void (*entry)(void), uint32_t stackSize);
Task* taskCreateUser(void (*entry)(void), uint32_t stackSize);
void taskYield(void);
void taskSchedule(void);

#endif