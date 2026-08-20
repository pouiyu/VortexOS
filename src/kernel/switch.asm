section .text
bits 32

global taskSwitch
extern currentTask
extern nextTask

taskSwitch:
    push ebx
    push esi
    push edi
    push ebp

    mov edi, [currentTask]
    mov [edi + 28], esp

    mov esi, [nextTask]
    mov [currentTask], esi
    mov esp, [esi + 28]

    pop ebp
    pop edi
    pop esi
    pop ebx
    ret