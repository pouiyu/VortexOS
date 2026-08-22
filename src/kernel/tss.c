#include "tss.h"
#include <string/string.h>

extern uint8_t gdt_tss[];   // boot.asm 的标签

typedef struct {
    uint32_t prevTss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomapBase;
} __attribute__((packed)) TssEntry;

static TssEntry tss;

void tssInit(uint32_t kernelStack) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0  = 0x10;
    tss.esp0 = kernelStack;

    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    uint8_t* entry = gdt_tss;

    entry[0] = limit & 0xFF;
    entry[1] = (limit >> 8) & 0xFF;
    entry[2] = base & 0xFF;
    entry[3] = (base >> 8) & 0xFF;
    entry[4] = (base >> 16) & 0xFF;
    entry[5] = 0x89;
    entry[6] = ((limit >> 16) & 0x0F);
    entry[7] = (base >> 24) & 0xFF;

    __asm__ volatile ("mov $0x2B, %%ax; ltr %%ax" : : : "ax");
}

uint32_t tss_esp0;

void tssSetEsp0(uint32_t esp0) {
    tss_esp0 = esp0;   // 全局变量（可选，用于调试）
    tss.esp0 = esp0;   // 关键！必须更新 TSS 结构体
}