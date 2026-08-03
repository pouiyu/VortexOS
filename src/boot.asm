; boot.asm - 32位 Multiboot2 启动加载器

section .multiboot
align 8
header_start:
    ; Multiboot2 头
    dd 0xE85250D6          ; magic number
    dd 0x00                ; architecture (0 = 32位保护模式)
    dd header_end - header_start ; header length
    dd -(0xE85250D6 + 0x00 + (header_end - header_start)) ; checksum

    ; 可选标签（结束标签必须存在）
    align 8
    dw 0x00                ; type
    dw 0x00                ; flags
    dd 0x08                ; size
header_end:

section .text
global _start
extern kernel_main          ; 不带下划线

; ============ GDT ============
gdt_start:
    ; 空描述符
    dd 0x00000000
    dd 0x00000000

; 代码段 - 选择子 0x08
gdt_code:
    dw 0xFFFF              ; limit low
    dw 0x0000              ; base low
    db 0x00                ; base middle
    db 10011010b           ; access (Present, Ring 0, Executable)
    db 11001111b           ; flags + limit high
    db 0x00                ; base high

; 数据段 - 选择子 0x10
gdt_data:
    dw 0xFFFF              ; limit low
    dw 0x0000              ; base low
    db 0x00                ; base middle
    db 10010010b           ; access (Present, Ring 0, Data)
    db 11001111b           ; flags + limit high
    db 0x00                ; base high
gdt_end:

gdt_pointer:
    dw gdt_end - gdt_start - 1
    dd gdt_start

_start:
    ; 加载 GDT
    lgdt [gdt_pointer]
    
    ; 长跳转到代码段（更新 CS）
    jmp 0x08:protected_mode

protected_mode:
    ; 设置数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; ============ 启用 SSE ============
    ; 检查并启用 SSE
    mov eax, cr0
    and ax, 0xFFFB         ; 清除 CR0.EM (bit 2)
    or ax, 0x2             ; 设置 CR0.MP (bit 1)
    mov cr0, eax
    
    mov eax, cr4
    or ax, 3 << 9          ; 设置 CR4.OSFXSR (bit 9) 和 CR4.OSXMMEXCPT (bit 10)
    mov cr4, eax
    
    ; 设置栈指针
    mov esp, stack_top
    
    ; 传递 Multiboot2 信息
    push ebx               ; Multiboot2 信息结构指针
    push eax               ; magic number
    
    ; 调用内核主函数
    call kernel_main       ; 不带下划线
    
    ; 如果返回，停机
    cli
    hlt

section .bss
align 16
stack_bottom:
    resb 16384             ; 16KB 栈空间
stack_top: