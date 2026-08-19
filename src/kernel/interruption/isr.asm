; isr.asm - 异常和中断入口

section .text
bits 32

extern exc0
extern exc1
extern exc2
extern exc3
extern exc4
extern exc5
extern exc6
extern exc7
extern exc8
extern exc9
extern exc10
extern exc11
extern exc12
extern exc13
extern exc14
extern exc15
extern exc16
extern exc17
extern exc18
extern exc19
extern exc20
extern exc21
extern exc22
extern exc23
extern exc24
extern exc25
extern exc26
extern exc27
extern exc28
extern exc29
extern exc30
extern exc31
extern keyboardIRQHandler
extern vgaPutStr
extern keyboardHasChar
extern keyboardGetChar


%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
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

    push esp
    call exc%1
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
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

    push esp
    call exc%1
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; 键盘中断处理入口
global irq1_handler

irq1_handler:
    pusha
    call keyboardIRQHandler
    popa
    iret

; 加载 IDT
global idtLoad

idtLoad:
    mov eax, [esp + 4]
    lidt [eax]
    ret


global syscall_entry
extern syscallHandler

syscall_entry:
    pusha

    cmp eax, 1          ; SYS_WRITE
    je .do_write
    cmp eax, 2          ; SYS_READ
    je .do_read
    cmp eax, 3          ; SYS_EXIT
    je .do_exit
    jmp .done

.do_write:
    push ebx
    call vgaPutStr
    add esp, 4
    jmp .done

.do_read:
    ; 等待键盘
.wait_key:
    call keyboardHasChar
    test eax, eax
    jz .wait_key

    call keyboardGetChar
    ; 存到 buf（ebx）
    mov [ebx], al
    jmp .done

.do_exit:
    cli
    hlt
    jmp .do_exit

.done:
    popa
    iret