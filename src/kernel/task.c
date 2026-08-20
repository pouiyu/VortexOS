#include "task.h"
#include <mm/pmm.h>
#include <string/string.h>

Task* currentTask = NULL;
Task* nextTask = NULL;

static Task taskList[16];
static int taskCount = 0;
static int currentId = -1;

void taskInit(void) {
    taskCount = 0;
    currentId = 0;
    currentTask = NULL;
    nextTask = NULL;
}

Task* taskCreate(void (*entry)(void), uint32_t stackSize) {
    if (taskCount >= 16) return NULL;

    Task* task = &taskList[taskCount];
    task->id = taskCount++;
    task->stackSize = stackSize;
    task->state = 0;

    // 分配内核栈
    task->stack = (uint32_t*)pmmAllocPage();
    if (!task->stack) return NULL;

    // 设置初始栈（模拟中断压栈）
    uint32_t* esp = (uint32_t*)((uint32_t)task->stack + stackSize);

    esp--; *esp = (uint32_t)entry;  // 返回地址（最低，栈顶）
    esp--; *esp = 0;                 // ebp
    esp--; *esp = 0;                 // edi
    esp--; *esp = 0;                 // esi
    esp--; *esp = 0;                 // ebx（最高）

    task->ctx.esp = (uint32_t)esp;

    return task;
}

void taskYield(void) {
    if (taskCount == 0) return;

    if (currentTask == NULL) {
        // 第一次启动任务0
        currentTask = &taskList[0];
        currentId = 0;
        nextTask = &taskList[0];

        // 直接加载任务0的 esp 并开始执行
        __asm__ volatile (
            "mov %0, %%esp\n"
            "pop %%ebp\n"
            "pop %%edi\n"
            "pop %%esi\n"
            "pop %%ebx\n"
            "ret\n"
            :
            : "r"(nextTask->ctx.esp)
        );
    }

    // 任务0执行到这里说明它调用了 taskYield
    currentId = (currentId + 1) % taskCount;
    nextTask = &taskList[currentId];
    taskSwitch();
}

void taskSchedule(void) {
    taskYield();
}