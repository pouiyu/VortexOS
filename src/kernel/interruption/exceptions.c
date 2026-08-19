#include "exceptions.h"
#include <vga.h>

static const char* exceptionNames[32] = {
    "Divide by Zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security Exception",
    "Reserved"
};

static void panic(ExceptionFrame* frame) {
    vgaSetColor(COLOR_WHITE, COLOR_RED);
    vgaClear();

    vgaPutStr("=== EXCEPTION ===\n\n");

    if (frame->intNum < 32) {
        vgaPutStr("Type: ");
        vgaPutStr(exceptionNames[frame->intNum]);
    } else {
        vgaPutStr("Unknown interrupt: 0x");
        vgaPutHex32(frame->intNum);
    }
    vgaPutStr("\n");

    vgaPutStr("Error code: 0x");
    vgaPutHex32(frame->errCode);
    vgaPutStr("\n");

    vgaPutStr("EIP: 0x");
    vgaPutHex32(frame->eip);
    vgaPutStr("  ESP: 0x");
    vgaPutHex32(frame->esp);
    vgaPutStr("  EBP: 0x");
    vgaPutHex32(frame->ebp);
    vgaPutStr("\n");

    vgaPutStr("CS: 0x");
    vgaPutHex32(frame->cs);
    vgaPutStr("  SS: 0x");
    vgaPutHex32(frame->ss);
    vgaPutStr("  EFLAGS: 0x");
    vgaPutHex32(frame->eflags);
    vgaPutStr("\n");

    vgaPutStr("UserESP: 0x");
    vgaPutHex32(frame->useresp);
    vgaPutStr("\n\n");

    vgaPutStr("Registers:\n");
    vgaPutStr("  EAX: 0x"); vgaPutHex32(frame->eax); vgaPutStr("\n");
    vgaPutStr("  EBX: 0x"); vgaPutHex32(frame->ebx); vgaPutStr("\n");
    vgaPutStr("  ECX: 0x"); vgaPutHex32(frame->ecx); vgaPutStr("\n");
    vgaPutStr("  EDX: 0x"); vgaPutHex32(frame->edx); vgaPutStr("\n");
    vgaPutStr("  ESI: 0x"); vgaPutHex32(frame->esi); vgaPutStr("\n");
    vgaPutStr("  EDI: 0x"); vgaPutHex32(frame->edi); vgaPutStr("\n");

    vgaPutStr("\nSegments:\n");
    vgaPutStr("  DS: 0x"); vgaPutHex32(frame->ds); vgaPutStr("\n");
    vgaPutStr("  ES: 0x"); vgaPutHex32(frame->es); vgaPutStr("\n");
    vgaPutStr("  FS: 0x"); vgaPutHex32(frame->fs); vgaPutStr("\n");
    vgaPutStr("  GS: 0x"); vgaPutHex32(frame->gs); vgaPutStr("\n");

    vgaPutStr("\nSystem halted.\n");

    for (;;) __asm__ volatile ("hlt");
}

void exc0(ExceptionFrame* frame)  { panic(frame); }  // 除零错误
void exc1(ExceptionFrame* frame)  { panic(frame); }  // 调试异常
void exc2(ExceptionFrame* frame)  { panic(frame); }  // 不可屏蔽中断
void exc3(ExceptionFrame* frame)  { panic(frame); }  // 断点
void exc4(ExceptionFrame* frame)  { panic(frame); }  // 溢出
void exc5(ExceptionFrame* frame)  { panic(frame); }  // 越界
void exc6(ExceptionFrame* frame)  { panic(frame); }  // 无效操作码
void exc7(ExceptionFrame* frame)  { panic(frame); }  // 设备不可用
void exc8(ExceptionFrame* frame)  { panic(frame); }  // 双重故障
void exc9(ExceptionFrame* frame)  { panic(frame); }  // 协处理器段越界
void exc10(ExceptionFrame* frame) { panic(frame); }  // 无效 TSS
void exc11(ExceptionFrame* frame) { panic(frame); }  // 段不存在
void exc12(ExceptionFrame* frame) { panic(frame); }  // 栈段错误
void exc13(ExceptionFrame* frame) { panic(frame); }  // 一般保护错误
void exc14(ExceptionFrame* frame) { panic(frame); }  // 页错误
void exc15(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc16(ExceptionFrame* frame) { panic(frame); }  // x87 浮点异常
void exc17(ExceptionFrame* frame) { panic(frame); }  // 对齐检查
void exc18(ExceptionFrame* frame) { panic(frame); }  // 机器检查
void exc19(ExceptionFrame* frame) { panic(frame); }  // SIMD 浮点异常
void exc20(ExceptionFrame* frame) { panic(frame); }  // 虚拟化异常
void exc21(ExceptionFrame* frame) { panic(frame); }  // 控制保护异常
void exc22(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc23(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc24(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc25(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc26(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc27(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc28(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc29(ExceptionFrame* frame) { panic(frame); }  // 保留
void exc30(ExceptionFrame* frame) { panic(frame); }  // 安全异常
void exc31(ExceptionFrame* frame) { panic(frame); }  // 保留