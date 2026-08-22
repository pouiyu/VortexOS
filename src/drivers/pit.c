#include "pit.h"
#include <io.h>
#include "task.h"
#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

void pitInit(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void pitHandler(void) {
    extern void taskTick(void);
    taskTick();
}