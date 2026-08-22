#include "task.h"
#include "tss.h"
#include <mm/pmm.h>
#include <mm/paging.h>
#include <string/string.h>

Task* currentTask = NULL;
Task* nextTask = NULL;

static Task taskList[MAX_TASKS];
static int taskCount = 0;

// 就绪队列（按优先级降序）
static Task* readyQueue[MAX_TASKS];
static int readyCount = 0;

void taskInit(void) {
    taskCount = 0;
    readyCount = 0;
    currentTask = NULL;
    nextTask = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        taskList[i].ctx.esp = 0;
        readyQueue[i] = NULL;
    }
}

static void setupTaskStack(Task* task, void (*entry)(void), int isUser) {
    uint32_t* kesp = (uint32_t*)((uint32_t)task->stack + task->stackSize);

    // 1. 压入 iret 帧（从高到低）
    if (isUser) {
        kesp--; *kesp = 0x23;   // ss
        kesp--; *kesp = (uint32_t)task->userStack + task->stackSize;  // useresp
        kesp--; *kesp = 0x202;  // eflags (IF=1)
        kesp--; *kesp = 0x1B;   // cs = 用户代码段
        kesp--; *kesp = (uint32_t)entry;  // eip
    } else {
        kesp--; *kesp = 0x202;  // eflags
        kesp--; *kesp = 0x08;   // cs = 内核代码段
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

static void enqueueTask(Task* task) {
    int i = 0;
    while (i < readyCount && readyQueue[i]->priority >= task->priority) i++;
    for (int j = readyCount; j > i; j--) readyQueue[j] = readyQueue[j-1];
    readyQueue[i] = task;
    readyCount++;
}

Task* taskCreate(void (*entry)(void), uint32_t stackSize, int priority) {
    if (taskCount >= MAX_TASKS) return NULL;

    Task* task = &taskList[taskCount];
    task->id = taskCount++;
    task->stackSize = stackSize;
    task->state = 0;
    task->isUser = 0;
    task->priority = priority;
    task->timeSlice = 2 + priority;      // 优先级高，时间片长
    task->ticksRemaining = task->timeSlice;

    task->stack = (uint32_t*)pmmAllocPage();
    if (!task->stack) return NULL;

    // 内核任务无需用户栈
    task->userStack = NULL;
    task->pageDir = 0;  // 内核任务不拥有独立页目录

    setupTaskStack(task, entry, 0);
    enqueueTask(task);
    return task;
}

Task* taskCreateUser(void (*entry)(void), uint32_t stackSize, int priority) {
    if (taskCount >= MAX_TASKS) return NULL;

    Task* task = &taskList[taskCount];
    task->id = taskCount++;
    task->stackSize = stackSize;
    task->state = 0;
    task->isUser = 1;
    task->priority = priority;
    task->timeSlice = 2 + priority;
    task->ticksRemaining = task->timeSlice;

    task->stack = (uint32_t*)pmmAllocPage();
    if (!task->stack) return NULL;

    task->userStack = (uint32_t*)pmmAllocPage();
    if (!task->userStack) return NULL;

    // 创建独立的用户页目录（与内核相同的身份映射副本）
    task->pageDir = pagingCreateUserDirectory();
    if (!task->pageDir) return NULL;

    setupTaskStack(task, entry, 1);
    enqueueTask(task);
    return task;
}

void taskSchedule(void) {
    if (readyCount == 0) return;

    // 当前任务即队首（在切换前）
    currentTask = readyQueue[0];

    // 将队首移到队尾（轮转）
    Task* temp = readyQueue[0];
    for (int i = 0; i < readyCount - 1; i++) readyQueue[i] = readyQueue[i+1];
    readyQueue[readyCount - 1] = temp;

    // 下一个任务为新队首
    nextTask = readyQueue[0];

    // 更新 TSS 的内核栈指针
    if (nextTask->isUser) {
        tssSetEsp0((uint32_t)nextTask->stack + nextTask->stackSize);
    } else {
        tssSetEsp0((uint32_t)nextTask->stack + nextTask->stackSize);
    }
}

void taskYield(void) {
    if (taskCount == 0) return;

    // 首次调用：启动第一个任务
    if (currentTask == NULL) {
        currentTask = readyQueue[0];
        nextTask = currentTask;

        tssSetEsp0((uint32_t)nextTask->stack + nextTask->stackSize);

        // 如果任务是用户态，设置用户段
        if (nextTask->isUser) {
            __asm__ volatile (
                "mov %0, %%esp\n"
                "mov $0x23, %%ax\n"
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
        } else {
            __asm__ volatile (
                "mov %0, %%esp\n"
                "mov $0x10, %%ax\n"
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
        }
        return;
    }

    // 正常切换：在中断处理中调用
    taskSchedule();
}

// 供 pitHandler 调用
void taskTick(void) {
    if (currentTask) {
        currentTask->ticksRemaining--;
        if (currentTask->ticksRemaining > 0) {
            return;  // 时间片未用完，不切换
        }
        currentTask->ticksRemaining = currentTask->timeSlice;
    }
    taskSchedule();
}