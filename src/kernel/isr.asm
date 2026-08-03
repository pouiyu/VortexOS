; isr.asm
section .text
global idtLoad
global irq1_handler

extern keyboardIRQHandler

idtLoad:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; IRQ1 键盘中断处理
irq1_handler:
    pusha
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    call keyboardIRQHandler
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    
    iret