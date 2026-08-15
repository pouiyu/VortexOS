#ifndef _KERNEL_INTERRUPTION_EXCEPTIONS_H
#define _KERNEL_INTERRUPTION_EXCEPTIONS_H

#include <stdint.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t intNum, errCode;
    uint32_t eip, cs, eflags, useresp, ss;
} ExceptionFrame;

void exc0(ExceptionFrame* frame);
void exc1(ExceptionFrame* frame);
void exc2(ExceptionFrame* frame);
void exc3(ExceptionFrame* frame);
void exc4(ExceptionFrame* frame);
void exc5(ExceptionFrame* frame);
void exc6(ExceptionFrame* frame);
void exc7(ExceptionFrame* frame);
void exc8(ExceptionFrame* frame);
void exc9(ExceptionFrame* frame);
void exc10(ExceptionFrame* frame);
void exc11(ExceptionFrame* frame);
void exc12(ExceptionFrame* frame);
void exc13(ExceptionFrame* frame);
void exc14(ExceptionFrame* frame);
void exc15(ExceptionFrame* frame);
void exc16(ExceptionFrame* frame);
void exc17(ExceptionFrame* frame);
void exc18(ExceptionFrame* frame);
void exc19(ExceptionFrame* frame);
void exc20(ExceptionFrame* frame);
void exc21(ExceptionFrame* frame);
void exc22(ExceptionFrame* frame);
void exc23(ExceptionFrame* frame);
void exc24(ExceptionFrame* frame);
void exc25(ExceptionFrame* frame);
void exc26(ExceptionFrame* frame);
void exc27(ExceptionFrame* frame);
void exc28(ExceptionFrame* frame);
void exc29(ExceptionFrame* frame);
void exc30(ExceptionFrame* frame);
void exc31(ExceptionFrame* frame);

#endif