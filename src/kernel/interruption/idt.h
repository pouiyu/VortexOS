#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H

#include <stdint.h>
#include <sys/cdefs.h>

typedef struct {
    uint16_t baseLow;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t baseHigh;
} __packed IDTEntry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __packed IDTPtr;

void idtInit(void);
void idtSetGate(uint8_t vector, void* handler, uint16_t selector, uint8_t flags);

#endif