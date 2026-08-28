#ifndef _KERNEL_INSTALL_H
#define _KERNEL_INSTALL_H

#include <stdbool.h>

/* 光驱根目录下整个 system 运行目录(构建期由 build_wsl.sh 自动拷入) */
#define CD_SYSTEM_DIR  "SYSTEM"

/* 光驱里的字体文件：随 system 目录分发(构建期从 system/font/font.bin 拷入) */
#define CD_FONT_PATH   "SYSTEM/FONT/FONT.BIN"
#define CD_FONT_CJK_PATH "SYSTEM/FONT/CJK16.BIN"

/* 硬盘上系统根目录与字体路径 */
#define DISK_SYSTEM_DIR "/system"
#define DISK_FONT_DIR  "/system/font"
#define DISK_FONT_PATH "/system/font/font.bin"
#define DISK_FONT_CJK_PATH "/system/font/cjk16.bin"

/* 光驱里的 GRUB 装盘镜像与系统文件(大写为 ISO9660 规范化形式) */
#define CD_GRUB_MBR_PATH  "GRUB/HDD_MBR.BIN"
#define CD_GRUB_CORE_PATH "GRUB/CORE.IMG"
#define CD_GRUB_CFG_PATH  "BOOT/GRUB/GRUB.CFG"
#define CD_KERNEL_PATH    "BOOT/KERNEL.BIN"

/* 硬盘上 GRUB 与根文件布局 */
#define DISK_BOOT_DIR   "/boot"
#define DISK_GRUB_DIR   "/boot/grub"
#define DISK_GRUB_CFG   "/boot/grub/grub.cfg"
#define DISK_KERNEL     "/boot/kernel.bin"
#define GRUB_SECTOR_START 1      /* core.img 写到扇区 1 起的 gap */

/* FAT32 初始化失败时调用：格式化硬盘 + 把字体从光驱拷到硬盘 */
bool installSystem(void);

/* 交互式文本模式安装向导的返回结果 */
typedef enum {
    INST_RESULT_BOOT,      /* 继续正常启动（无需安装/拒绝更新） */
    INST_RESULT_RUN_CD     /* 选择从 CD-ROM 运行本轮 */
} InstallResult;

/* 交互式文本模式安装向导：格式化安装 / 从 CD 运行（未格式化时），
 * 或比较后询问是否更新（已格式化且不同时）。安装完成会在向导内自动重启。 */
InstallResult installWizard(void);

/* 开机自动加载：从光驱读取字体并写入硬盘（若尚未存在） */
bool loadFontFromCd(void);

/* 读回验证：打开硬盘上的字体并打印大小 */
void verifyFontOnDisk(void);

/* 把硬盘上的 font.bin 读进内存并注册给 VBE（8x16 渲染） */
void loadFontIntoVbe(void);

/* 把硬盘上的 cjk16.bin 读进内存并注册给 VBE（16x16 中文渲染） */
void loadCjkFontIntoVbe(void);

/* “从 CD 运行本轮”：不写盘，字体直接读自光驱并加载进 VBE */
void loadFontFromCdIntoVbe(void);

#endif