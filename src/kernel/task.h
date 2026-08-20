#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
} TaskContext;

typedef struct {
    TaskContext ctx;
    uint32_t* stack;      // 内核栈
    uint32_t stackSize;
    int state;            // 0=就绪, 1=运行, 2=阻塞
    int id;
} Task;

void taskInit(void);
Task* taskCreate(void (*entry)(void), uint32_t stackSize);
void taskYield(void);
void taskSchedule(void);

extern Task* currentTask;
extern Task* nextTask;

#endif