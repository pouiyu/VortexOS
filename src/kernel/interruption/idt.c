#include "idt.h"
#include "exceptions.h"
#include <string/string.h>
#include <vga.h>
#include <io.h>

// isr 入口
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void irq0_handler(void);

void exceptionsInit(void) {
    idtSetGate(0x00, isr0,  0x08, 0x8E);
    idtSetGate(0x01, isr1,  0x08, 0x8E);
    idtSetGate(0x02, isr2,  0x08, 0x8E);
    idtSetGate(0x03, isr3,  0x08, 0x8E);
    idtSetGate(0x04, isr4,  0x08, 0x8E);
    idtSetGate(0x05, isr5,  0x08, 0x8E);
    idtSetGate(0x06, isr6,  0x08, 0x8E);
    idtSetGate(0x07, isr7,  0x08, 0x8E);
    idtSetGate(0x08, isr8,  0x08, 0x8E);
    idtSetGate(0x09, isr9,  0x08, 0x8E);
    idtSetGate(0x0A, isr10, 0x08, 0x8E);
    idtSetGate(0x0B, isr11, 0x08, 0x8E);
    idtSetGate(0x0C, isr12, 0x08, 0x8E);
    idtSetGate(0x0D, isr13, 0x08, 0x8E);
    idtSetGate(0x0E, isr14, 0x08, 0x8E);
    idtSetGate(0x0F, isr15, 0x08, 0x8E);
    idtSetGate(0x10, isr16, 0x08, 0x8E);
    idtSetGate(0x11, isr17, 0x08, 0x8E);
    idtSetGate(0x12, isr18, 0x08, 0x8E);
    idtSetGate(0x13, isr19, 0x08, 0x8E);
    idtSetGate(0x14, isr20, 0x08, 0x8E);
    idtSetGate(0x15, isr21, 0x08, 0x8E);
    idtSetGate(0x16, isr22, 0x08, 0x8E);
    idtSetGate(0x17, isr23, 0x08, 0x8E);
    idtSetGate(0x18, isr24, 0x08, 0x8E);
    idtSetGate(0x19, isr25, 0x08, 0x8E);
    idtSetGate(0x1A, isr26, 0x08, 0x8E);
    idtSetGate(0x1B, isr27, 0x08, 0x8E);
    idtSetGate(0x1C, isr28, 0x08, 0x8E);
    idtSetGate(0x1D, isr29, 0x08, 0x8E);
    idtSetGate(0x1E, isr30, 0x08, 0x8E);
    idtSetGate(0x1F, isr31, 0x08, 0x8E);
}

#define IDT_SIZE 256

static IDTEntry idt[IDT_SIZE];
static IDTPtr idtPtr;

extern void idtLoad(IDTPtr* ptr);
extern void irq1_handler(void);

void idtSetGate(uint8_t vector, void* handler, uint16_t selector, uint8_t flags) {
    uint32_t addr = (uint32_t)handler;
    idt[vector].baseLow = addr & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].zero = 0;
    idt[vector].flags = flags;
    idt[vector].baseHigh = (addr >> 16) & 0xFFFF;
}

void idtInit(void) {
    for (int i = 0; i < IDT_SIZE; i++) {
        idt[i].baseLow = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].flags = 0;
        idt[i].baseHigh = 0;
    }
    
    idtPtr.limit = sizeof(idt) - 1;
    idtPtr.base = (uint32_t)idt;

    // 注册所有门（在 idtLoad 之前）
    idtSetGate(0x21, irq1_handler, 0x08, 0x8E);
    idtSetGate(0x20, irq0_handler, 0x08, 0x8E);
    exceptionsInit();

    idtLoad(&idtPtr);

    // PIC 初始化
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // 打开 IRQ0 和 IRQ1
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}