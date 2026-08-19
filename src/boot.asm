; boot.asm - 32位 Multiboot2 启动加载器

section .multiboot
align 8
header_start:
    dd 0xE85250D6
    dd 0x00
    dd header_end - header_start
    dd -(0xE85250D6 + 0x00 + (header_end - header_start))

    align 8
    dw 0x00
    dw 0x00
    dd 0x08
header_end:

section .data
; ============ GDT ============
gdt_start:
    dd 0x00000000
    dd 0x00000000

gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_user_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 11111010b
    db 11001111b
    db 0x00

gdt_user_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 11110010b
    db 11001111b
    db 0x00

gdt_tss:
    dd 0x00000000
    dd 0x00000000

gdt_end:

gdt_pointer:
    dw gdt_end - gdt_start - 1
    dd gdt_start

global gdt_tss
gdt_tss_label:
    dd gdt_tss

section .text
global _start
extern kernel_main

_start:
    lgdt [gdt_pointer]
    jmp 0x08:protected_mode

protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, eax

    mov eax, cr4
    or ax, 3 << 9
    mov cr4, eax

    mov esp, stack_top

    push ebx
    push eax

    call kernel_main

    cli
    hlt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: