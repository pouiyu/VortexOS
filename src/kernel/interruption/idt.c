#include "idt.h"
#include <string.h>
#include <vga.h>
#include <io.h>

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
    
    idtSetGate(0x21, irq1_handler, 0x08, 0x8E);
    
    idtLoad(&idtPtr);
    
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    outb(0x21, 0xFD);
    outb(0xA1, 0xFF);
    
    vgaPutStrColor("[OK] IDT initialized\n", COLOR_LIGHT_GREEN);
}