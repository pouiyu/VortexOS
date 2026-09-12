/* user.ld —— 用户态程序链接脚本(独立 ELF)
 * 加载地址 0x400000：处于 PMM 保留区([4M,8M)，见 pmm.c kernelEnd)，
 * 不与会动态分配的窗口 surface/合成缓冲冲突。用户栈在 0x300000(向下生长)。
 * 身份映射下该地址 = 物理地址，elfLoad 直接写段数据到 vaddr。 */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text ALIGN(4K) :
    {
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss*)
    }

    end = .;
}
