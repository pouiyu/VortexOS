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
    vgaClear();

    vgaSetColor(COLOR_WHITE, COLOR_RED);
    vgaPutStr("EXCEPTION: ");
    vgaPutStr(exceptionNames[frame->intNum]);
    vgaPutStr("\n\n");
    vgaPutStr("Error code: 0x");
    vgaPutHex32(frame->errCode);
    vgaPutStr("\n");
    vgaPutStr("EIP: 0x");
    vgaPutHex32(frame->eip);
    vgaPutStr("\n");
    vgaPutStr("ESP: 0x");
    vgaPutHex32(frame->esp);
    vgaPutStr("\n");
    for (;;) __asm__ volatile ("hlt");
}

void exc0(ExceptionFrame* frame) { panic(frame); }
void exc1(ExceptionFrame* frame) { panic(frame); }
void exc2(ExceptionFrame* frame) { panic(frame); }
void exc3(ExceptionFrame* frame) { panic(frame); }
void exc4(ExceptionFrame* frame) { panic(frame); }
void exc5(ExceptionFrame* frame) { panic(frame); }
void exc6(ExceptionFrame* frame) { panic(frame); }
void exc7(ExceptionFrame* frame) { panic(frame); }
void exc8(ExceptionFrame* frame) { panic(frame); }
void exc9(ExceptionFrame* frame) { panic(frame); }
void exc10(ExceptionFrame* frame) { panic(frame); }
void exc11(ExceptionFrame* frame) { panic(frame); }
void exc12(ExceptionFrame* frame) { panic(frame); }
void exc13(ExceptionFrame* frame) { panic(frame); }
void exc14(ExceptionFrame* frame) { panic(frame); }
void exc15(ExceptionFrame* frame) { panic(frame); }
void exc16(ExceptionFrame* frame) { panic(frame); }
void exc17(ExceptionFrame* frame) { panic(frame); }
void exc18(ExceptionFrame* frame) { panic(frame); }
void exc19(ExceptionFrame* frame) { panic(frame); }
void exc20(ExceptionFrame* frame) { panic(frame); }
void exc21(ExceptionFrame* frame) { panic(frame); }
void exc22(ExceptionFrame* frame) { panic(frame); }
void exc23(ExceptionFrame* frame) { panic(frame); }
void exc24(ExceptionFrame* frame) { panic(frame); }
void exc25(ExceptionFrame* frame) { panic(frame); }
void exc26(ExceptionFrame* frame) { panic(frame); }
void exc27(ExceptionFrame* frame) { panic(frame); }
void exc28(ExceptionFrame* frame) { panic(frame); }
void exc29(ExceptionFrame* frame) { panic(frame); }
void exc30(ExceptionFrame* frame) { panic(frame); }
void exc31(ExceptionFrame* frame) { panic(frame); }