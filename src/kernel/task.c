#include "task.h"
#include <mm/pmm.h>
#include <string/string.h>
#include "tss.h"

Task* currentTask = NULL;
Task* nextTask = NULL;

static Task taskList[MAX_TASKS];
static int taskCount = 0;
static int currentId = -1;

void taskInit(void) {
    taskCount = 0;
    currentId = -1;
    currentTask = NULL;
    nextTask = NULL;
}

static void setupTaskStack(Task* task, void (*entry)(void), int isUser) {
    uint32_t* kesp = (uint32_t*)((uint32_t)task->stack + task->stackSize);

    // 1. 压入 iret 帧（从高到低）
    if (isUser) {
        kesp--; *kesp = 0x23;   // ss
        kesp--; *kesp = (uint32_t)task->userStack + task->stackSize;  // useresp
        kesp--; *kesp = 0x202;  // eflags
        kesp--; *kesp = 0x1B;   // cs
        kesp--; *kesp = (uint32_t)entry;  // eip
    } else {
        kesp--; *kesp = 0x202;  // eflags
        kesp--; *kesp = 0x08;   // cs
        kesp--; *kesp = (uint32_t)entry;  // eip
    }

    uint32_t* eip_addr = kesp;  // 记录 eip 地址

    // 2. 压入 pusha 帧（从高到低，最后压 edi）
    kesp--; *kesp = 0;  // eax
    kesp--; *kesp = 0;  // ecx
    kesp--; *kesp = 0;  // edx
    kesp--; *kesp = 0;  // ebx
    kesp--; *kesp = (uint32_t)eip_addr;  // esp 字段设为 eip 地址
    kesp--; *kesp = 0;  // ebp
    kesp--; *kesp = 0;  // esi
    kesp--; *kesp = 0;  // edi（栈顶）

    task->ctx.esp = (uint32_t)kesp;
}

Task* taskCreate(void (*entry)(void), uint32_t stackSize) {
    if (taskCount >= MAX_TASKS) return NULL;

    Task* task = &taskList[taskCount];
    task->id = taskCount++;
    task->stackSize = stackSize;
    task->state = 0;
    task->isUser = 0;

    task->stack = (uint32_t*)pmmAllocPage();
    if (!task->stack) return NULL;

    setupTaskStack(task, entry, 0);
    return task;
}

Task* taskCreateUser(void (*entry)(void), uint32_t stackSize) {
    if (taskCount >= MAX_TASKS) return NULL;

    Task* task = &taskList[taskCount];
    task->id = taskCount++;
    task->stackSize = stackSize;
    task->state = 0;
    task->isUser = 1;

    task->stack = (uint32_t*)pmmAllocPage();
    if (!task->stack) return NULL;

    task->userStack = (uint32_t*)pmmAllocPage();
    if (!task->userStack) return NULL;

    setupTaskStack(task, entry, 1);
    return task;
}

void taskYield(void) {
    if (taskCount == 0) return;

    if (currentTask == NULL) {
        currentTask = &taskList[0];
        currentId = 0;
        nextTask = &taskList[0];

        tssSetEsp0((uint32_t)nextTask->stack + nextTask->stackSize);

        __asm__ volatile (
            "mov %0, %%esp\n"
            "mov $0x23, %%ax\n"      // 用户数据段（因为当前只测用户任务）
            "mov %%ax, %%ds\n"
            "mov %%ax, %%es\n"
            "mov %%ax, %%fs\n"
            "mov %%ax, %%gs\n"
            "popa\n"
            "iret\n"
            :
            : "r"(nextTask->ctx.esp)
            : "ax"
        );
        return;
    }

    currentId = (currentId + 1) % taskCount;
    nextTask = &taskList[currentId];

    tssSetEsp0((uint32_t)nextTask->stack + nextTask->stackSize);
}

void taskSchedule(void) {
    taskYield();
}