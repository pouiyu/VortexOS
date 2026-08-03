/* 入口点 */
ENTRY(_start)

/* 内存布局 */
SECTIONS
{
    /* 内核加载地址（32位 x86） */
    . = 1M;

    /* 多引导头（必须位于前8KB） */
    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }

    /* 代码段 */
    .text ALIGN(4K) :
    {
        *(.text)
    }

    /* 只读数据段 */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    /* 初始化数据段 */
    .data ALIGN(4K) :
    {
        *(.data)
    }

    /* BSS段（未初始化数据） */
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }

    /* 内核结束地址（供C代码使用） */
    end = .;
    _end = .;
    __end = .;
}