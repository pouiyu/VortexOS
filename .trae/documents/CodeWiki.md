# VortexOS 漩涡操作系统 — Code Wiki

> 一个从零编写的 x86 32 位 hobby 操作系统。采用 **Multiboot/Grub** 引导，**GRUB 帧缓冲**作为全设备唯一可靠的图形路径，自带文本框、SATA/IDE/光驱、FAT32/ISO9660 双文件系统、多任务、系统调用、图形窗口管理器与可视化 UI 设计器。

---

## 目录
1. [项目概览](#1-项目概览)
2. [总体架构](#2-总体架构)
3. [目录结构](#3-目录结构)
4. [模块职责与关键接口](#4-模块职责与关键接口)
5. [关键数据结构](#5-关键数据结构)
6. [依赖关系](#6-依赖关系)
7. [构建与运行方式](#7-构建与运行方式)
8. [系统调用 ABI](#8-系统调用-abi)

---

## 1. 项目概览

| 项目 | 说明 |
|------|------|
| 名称 | VortexOS（漩涡操作系统） |
| 架构 | x86 / 32 位（`-m32`，`elf_i386`） |
| 引导 | Multiboot2 + GRUB（CD El Torito / 硬盘 memdisk / MBR） |
| 内核模型 | 单内核 + 用户态 ELF 程序（身份映射，同一地址空间） |
| 图形 | VBE LFB 图形模式 + GRUB 帧缓冲回退 + VGA 文本模式 |
| 文件系统 | FAT32（硬盘）、ISO9660（CD）、GRUB module 后备（内存） |
| 输入 | PS/2 键盘、PS/2 鼠标（USB XHCI/HID 暂停开发） |
| 开发环境 | WSL + gcc/ld/nasm/grub/xorriso/qemu |

**特性亮点**
- 开机直接进入图形化桌面（壁纸 + 任务栏 + 中文标题），失败回退文本主菜单。
- 自带合成式窗口管理器（标题栏 / 关闭 / 最小化 / 拖拽 / 聚焦 / z 序）。
- 用户态 GUI 程序以独立 ELF 加载至 `0x400000`，经 `int 0x80` 调用内核 WM 服务。
- 内置 `libgui`（GUI 封装）与 `libwidget`（按钮/标签/文本框等 6 组件）两套用户库。
- `tools/ui_designer.py` 可视化设计器，所见即所得生成 C 组件代码并一键编译。

---

## 2. 总体架构

系统采用**分层 + 模块化**结构，自底向上为：Boot → 驱动 → 内核核心 → 文件系统 → 图形 → 窗口服务 → 用户态 GUI。

```
┌──────────────────────────────────────────────────────────────┐
│  用户态 ELF        My_UI.elf (system/programs)                │
│             libwidget → libgui → int 0x80 (guiapi ABI)        │
├──────────────────────────────────────────────────────────────┤
│  WM 服务    wm/wmsvc.c  事件循环/合成/LFB回写/鼠标指针          │
│             wm/window.c 窗口管理 API                           │
│  Shell      shell/      命令解释器 + 512行滚动回看              │
│  Inst        install.c  安装/更新向导（文本模式）              │
├──────────────────────────────────────────────────────────────┤
│  图形/文本   lib/stdio  vbe.c(VBE LFB)   vga.c(VGA文本)        │
│  库          lib/stdlib string math                            │
├──────────────────────────────────────────────────────────────┤
│  文件系统    fs/  fat32.c iso9660.c file.c(含模块后备) path bmp │
├──────────────────────────────────────────────────────────────┤
│  内核核心    task/tss/syscall/user/device/elf  mm/  interruption│
├──────────────────────────────────────────────────────────────┤
│  驱动        ata/ahci/atapi/disk/keyboard/mouse/pit/rtc/serial  │
│             sound  usb(pci/xhci/hid:暂停)                     │
├──────────────────────────────────────────────────────────────┤
│  Boot        boot.asm → kernel_main  (Multiboot2)             │
└──────────────────────────────────────────────────────────────┘
```

**关键机制**
- **启动流程**：GRUB 载入 `kernel.bin` → `_start`（boot.asm 加载 GDT 进入保护模式）→ `kernel_main` 依次初始化 PMM、分页、串口、TSS、任务、VGA、IDT、系统调用、键盘 → 保存 MB2/注册模块 → ATAPI/磁盘枚举 → 采用引导器帧缓冲 → FAT32 挂载 → 安装/更新向导（文本）→ 字体加载 → `graphic_main` 直接进入图形桌面。
- **图形路径**：真机没有 Bochs dispi 端口，因此**唯一可靠方案是 GRUB 帧缓冲**；`graphic_main` 先 `vbeUseBootloaderFramebuffer` 采用之，再 `vbeSetMode`（`sFromBootloader` 短路复用）。`vbeSetMode` 失败必须立刻 `return`，否则整帧 `memcpy` 到 `lfbAddr=0` 会页错误崩死。
- **整帧合成**：每帧把背景 `bg` + 可见窗口合成到 RAM `shadow` 缓冲，再整帧写回 LFB（避免 QEMU bochs LFB 可写不可回读导致的读 LFB 页错误）。

---

## 3. 目录结构

```
vortex-os/
├── src/
│   ├── boot.asm              # 启动汇编入口（GDT/保护模式 → kernel_main）
│   ├── lib/                  # 内核库
│   │   ├── io.h              # 端口 I/O 内联函数（inb/outb/inl/outl）
│   │   ├── stdio/            # stdio.h（FILE/stdio 声明）、vbe.c/h、vga.c/h
│   │   ├── stdlib/ stdlib.c/h# 堆(malloc)、atoi、rand、qsort…
│   │   ├── string/ string.c/h
│   │   └── math/   math.c/h
│   ├── include/              # 公共头
│   │   ├── stdint/stddef/stdbool/stdarg.h
│   │   ├── sys/cdefs.h
│   │   ├── gui/   libgui.h   libwidget.h
│   │   └── wm/    window.h   wmsvc.h   guiapi.h
│   ├── drivers/              # 硬件驱动
│   │   ├── ata.c/h  atapi.c/h  ahci.c/h  disk.c/h
│   │   ├── keyboard  mouse  pit  rtc  serial  sound
│   │   └── usb/pci  usb/xhci  usb/hid（#if 0 暂停）
│   ├── kernel/               # 内核核心
│   │   ├── kernel.c          # kernel_main、文本主菜单、graphic_main
│   │   ├── install.c/h       # 安装/更新向导
│   │   ├── device.c/h        # 重启/关机
│   │   ├── task.c/h tss.c/h syscall.c/h user.c/h
│   │   ├── mm/   pmm.c/h paging.c/h
│   │   ├── interruption/ idt.c exceptions.c isr.asm
│   │   ├── fs/   fat32.c iso9660.c file.c path.c bmp.c
│   │   ├── shell/ shell.c commands.c
│   │   ├── sysinfo/ sysinfo.c config.h
│   │   ├── elf/elf.c/h
│   │   ├── vex/vex.c/h      # VEX 可执行装载器
│   │   └── wm/ window.c wmsvc.c
│   └── user/                 # 用户态（独立 ELF，不链接进内核）
│       └── libgui.c libwidget.c My_UI.c
├── system/                   # 运行目录（整树拷进 ISO 与硬盘）
│   ├── font/ font.bin cjk16.bin
│   ├── images/ wallpaper.bmp taskbar.bmp vortex.bmp mousePointer/*
│   └── programs/ My_UI.elf
├── iso/                      # 构建产物（CD 树 + 引导镜像）
├── tools/                    # 构建与开发工具（build_wsl.sh 等）
├── build/                    # 编译中间 .o
├── Makefile   linker.ld   user.ld
└── vortexos.iso  disk.img … # 运行产物
```

---

## 4. 模块职责与关键接口

### 4.1 启动与内核入口
| 文件 | 职责 / 关键函数 |
|------|------|
| `src/boot.asm` | Multiboot 头、GDT、保护模式切换、调用 `kernel_main` |
| `kernel/kernel.c` | `kernel_main`（初始化序列 + 安装向导 + 字体 + 进图形）；`graphic_main`（图形初始化、背景/任务栏/中文一次性渲染、WM 初始化、加载用户 ELF 并跳转）；文本主菜单与子菜单状态机 |

### 4.2 内存管理（`kernel/mm/`）
| 函数 | 说明 |
|------|------|
| `pmmInit(max)`, `pmmAllocPage`, `pmmAllocPages`, `pmmFreePage` | 物理内存位图分配/释放。`[4M,8M)` 保留给用户态 GUI 程序；8MB 以上标空闲 |
| `pagingInit` | 为内核建 64 个页目录项，映射 `PRESENT\|WRITABLE\|USER`，载入 cr3 并开分页 |
| `pagingMapPage`, `pagingUnmapPage` | 动态页映射/取消 |
| `pagingCreateUserDirectory` | 复制内核页表创建用户页目录 |

### 4.3 中断与异常（`kernel/interruption/`）
| 文件/函数 | 说明 |
|------|------|
| `idtSetGate`, `idtInit` | 填 IDT 门描述符；注册异常(0x00–0x1F)与 IRQ0/1/12，初始化 PIC |
| `exceptionsInit` | 绑定 `isrN` → 异常入口 |
| `panik(frame)` | 记录异常名/cr2/错误码/EIP/ESP/EBP 后停机（当前策略） |
| `isr.asm` | ISR 公共宏 `ISR_NOERR`/`ISR_ERR`，保存/恢复寄存器并调 C 处理器 |

### 4.4 任务与用户态（`kernel/`）
| 文件/函数 | 说明 |
|------|------|
| `taskCreate`, `taskCreateUser`, `taskYield`, `taskTick` | 轮转调度 + 时间片；用户任务分配用户栈/页目录；切换更新 TSS esp0 |
| `tssInit`, tssSetEsp0 | TSS 初始化与内核栈指针更新 |
| `syscallInit`, `syscallDispatch` | 将 `int 0x80` 注册进 IDT；按调用号分发到 WM 服务 |
| `jumpToUserGui`, `sysExitKernel` | 从内核 `iret` 到用户程序；程序退出时恢复内核栈与续点（回到 `guiExitToMenu`） |
| `elfLoad` | ELF 加载器：校验魔数/架构，定位 `PT_LOAD` 段写入虚拟地址、清零 BSS，返回入口 |
| `deviceReboot`/`deviceShutdown` | I/O port 0x64 重启 / 清屏 halt 关机 |

### 4.5 文件系统（`kernel/fs/`）
| 文件 | 职责 / 关键函数 |
|------|------|
| `fat32.c/h` | `fat32Volume` 卷结构；`fat32Init` 解析 MBR/VBR；LFN 长文件名；`fat32FindFreeCluster` 用 `nextFreeCluster` 续查实现连续簇分配；创建/读写/删除/改名/复制 |
| `iso9660.c/h` | CD 只读文件系统，`iso9660ListDir(name,ext,len,flags,arg)` 目录遍历回调 |
| `file.c/h` | 抽象文件层 `fsOpen/fsRead/fsClose`；FAT32 优先、失败回退 GRUB 模块后备；`fsRegisterModules` 解析 MB2 tag type 3、`fsFindModule`（strcasecmp 不区分大小写）、`kModuleSysFiles` 固定清单 |
| `path.c` | 路径解析 |
| `bmp.c/h` | `BmpImage{pixels,w,h}` 位图解析 |

> 内存模块后备是关键：U盘/光驱 AHCI 或无格式化盘时，`fsRead` 直接内存拷贝；`fsRead` 必须走 `driveReadSectors(fsVolume.drive)` 抽象层，禁止直调 `ataReadSector`（AHCI 真机会读错盘）。

### 4.6 驱动（`drivers/`）
| 驱动 | 职责 / 关键接口 |
|------|------|
| `ata.c/h` | IDE PIO：`ataReadSector/ataWriteSector`，0x1F0 端口，状态等待 |
| `ahci.c/h` | SATA AHCI：PCI 探测、BAR5 identity map、`ahciReadSectors/WriteSectors` |
| `atapi.c/h` | CD-ROM：IDENTIFY PACKET、PIO 包命令读 2048B 逻辑扇区 |
| `disk.c/h` | 磁盘抽象层：枚举 IDE+AHCI 到 `gDrives`，`driveReadSectors/WriteSectors` 按 `DriveType` 分发 |
| `keyboard.c` | PS/2 IRQ1：扫描码集 1/2 检测、扩展码/释放码状态机、环形缓冲、`keyboardGetChar` |
| `mouse.c` | PS/2 IRQ12：3 字节包解析、位移/按钮、绝对位置与事件缓冲；`mouseInit/SetBounds/SetPosition` |
| `pit.c` | 可编程定时器（时钟/调度） |
| `rtc.c` | 实时时钟 |
| `serial.c` | COM1 串口调试输出 |
| `sound.c` | 音频 |
| `usb/pci,xhci,hid` | USB（开发暂停，`#if 0` 摘除） |

> **鼠标启动时序**：不在启动时初始化 PS/2 鼠标（老笔记本 EC 对 8042 命令口 0x64 敏感会 SMI 挂起），仅在进图形时延迟初始化。

### 4.7 图形与文本（`lib/stdio/`）
采用**单当前色模型**：所有绘制函数不接收颜色参数，统一用 `vbeSetColor` 设置的当前色。

| 函数 | 语义 |
|------|------|
| `vbeSetMode(w,h,bpp)`, `vbeDisable` | 进入/退出 32bpp LFB；失败安全返回 |
| `vbeUseBootloaderFramebuffer` | 采用 GRUB 帧缓冲（真机唯一可靠路径） |
| `vbeClearScreen` | 当前色填充目标 |
| `vbeDrawPixel`, `vbeDrawFillRect`, `vbeDrawLineRect` | 点/填充矩形/线框矩形（包围盒内接，越界裁剪） |
| `vbeDrawLine` | Bresenham 直线 |
| `vbeDrawFillEllipse`, `vbeDrawLineEllipse` | 椭圆（(x,y) 左上 + w×h 包围盒，按 `(dx/rx)²+(dy/ry)²` 判定） |
| `vbeDrawBitmap(x,y,bgra,w,h)` | 自顶向下 BGRA，**Alpha 混合**（a=0 跳过透明、255 覆盖） |
| `vbeDrawChar`, `vbeDrawString` | ASCII 8×8/8×16 渲染 |
| `vbeDrawStringCJK` | UTF-8 解码：<0x80 用 8×16，汉字 16×16 双宽（内部 cjkFindGlyph 二分查找、utf8Decode） |
| `vbeBeginRamFrame/EendRamFrame` | 整帧 RAM 快照重定向 |
| `vbeBeginRamWindow/EendRamWindow` | 窗口 surface（客户区坐标→屏幕换算裁剪） |
| `vbeLoadFont`, `vbeLoadCjkFont` | 注入字库 |
| `vbeFbTextCell` | bpp 无关帧缓冲文本（8/15/16/24/32bpp 均可） |
| `vgaInit/vgaClear/vgaPutChar/vgaPutStr` | 0xB8000 文本前端 |
| `vgaSetCursorPos/SetCursorStyle/Enable/DisableCursor` | 光标控制（菜单禁用光标） |
| `vgaLoadFont` | 把 8×16 点阵上传到 VGA 字模平面 plane2 |
| `vgaSetFramebufferOutput/Flush` | 文本差异刷新到 LFB（真机控制台） |
| `vgaScrollView` | 512 行滚动回看（Ctrl+↑/↓） |

### 4.8 窗口管理器（`kernel/wm/`）
合成式 WM，窗口数组序即 z 序（尾部最上层）。

| 文件 | 函数 |
|------|------|
| `window.c/h` | `wmCreate`(满 16 返 -1)、`wmComposite`/`wmCompositeExcluding`/`wmCompositeOnly`、`wmHitTest`/`wmHitAction`、`wmFocus`、`wmGetPos`/`wmGetRect`、`wmMove`(拖拽+clamp)、`wmSetBodyColor/TitleColor/Title/Draw`、`wmGet/Set/Add/RemoveStyle`、`wmClose/Minimize/Restore`、`wmWindowCount`、客户区 `wmFillRect/wmDrawText/wmDrawLine` |
| `wmsvc.c/h` | `wmsvcInit(shadow,dragBack,bgPix)`、`wmsvcSetArrow`、`wmsvcHandle(fn,a,b,c)`(SYS_WM_* 分发)、`wmsvcPoll(WmEvent*)`(输入事件循环)、`wmsvcFlush`(全帧合成上屏)。拖拽走 L 形局部刷新 |

**拖拽优化**：按下时用 `wmCompositeExcluding` 建立"静止背景"`dragBack`，拖拽中仅 `wmCompositeOnly` 叠加被拖窗口 + 新旧位置包围盒 L 形区域写 LFB，避免每帧重画其它窗口文字。

### 4.9 用户态 GUI 库（`src/user/`）
| 文件 | 说明 |
|------|------|
| `libgui.c/h` | 封装 syscall，每函数只做参数打包 + `int 0x80`。`guiCreateWindow/guiSetColor/guiFillRect/guiDrawText/guiDrawLine/guiDrawLineRect/guiSetTitle/guiGetWinPos/guiClose/guiFlush/guiPoll/guiExit/guiRgb` |
| `libwidget.c/h` | 6 组件（`WD_BUTTON/LABEL/TEXTBOX/CHECKBOX/LINE/RECT`），统一 `Widget` 数组，无堆分配。`wdInit/wdDraw/wdHit/wdPress/wdHandleKey` |
| `My_UI.c` | 设计器生成的演示程序：建 360×240 窗口"My UI" + 6 组件，事件循环交互 |

`Widget` 字段序：`type,x,y,w,h,label,fg,bg,border,down,buf,cap,len,checked`（`buf` 指向调用方静态数组，`cap` 为容量）。

### 4.10 安装 / Shell / 系统信息
| 模块 | 说明 |
|------|------|
| `install.c` | `installWizard` 状态机（未格式化→询问；已格式化且 CD 不同→询问更新）；`installSystem`/`installWriteGrub`(写 hdd_mbr.bin+core.img)/`installBootFiles`/`installCopySystem`(整树递归，无 CD 走 `installCopySystemFromModules`)；`InstallResult{INST_RESULT_BOOT, INST_RESULT_RUN_CD}`；字体 `loadFontFromCd/IntoVbe/IntoVga`、`verifyFontOnDisk` |
| `shell/` | `runShell` 主循环，`shellCommand` 命令表。命令：help/clear/info/date/time/ls/cat/cd/pwd/mk/wr/cp/rm/ren/fg/bg/hl/ll/vex/reboot/shutdown/echo/exit。历史 32、输入 256、512 行滚动回看 |
| `sysinfo/` | `showSystemInfo/showDeviceInfo`；`config.h` 定义 OS_NAME=“vortex”、版本、LOGO |
| `vex/` | VEX 自定义可执行装载：`vexLoadAndRun(filename)`，按 `VexHeader` 分页加载并 `taskCreateUser` 运行（Shell `vex` 命令调用） |

---

## 5. 关键数据结构

- `fat32Volume`：drive、簇/扇区几何、根/数据目录扇区、`nextFreeCluster`（簇分配续查点）。
- `vbeInfo`（gVbeInfo）：lfbAddr, vramSize, pitch, xres, yres, bpp, enabled。
- `Window`：x,y,w,h、title/titleColor/bodyColor、style（`WM_STYLE_BORDER/TITLE/MINIMIZE/CLOSE/DRAG`）、visible/minimized、draw 回调、客户区 surface、drawColor。
- `WmEvent`：mouseX/Y、buttons、leftDown、key、flags。
- `WmCreateArgs`：title、x,y,w,h、titleColor、bodyColor、style。
- `MbFbInfo`：addr, pitch, width, height, bpp（Multiboot2 tag 8）。
- `Widget`：见 4.9。
- `VexHeader`：magic/version/entryPoint、code/data fileOffset/fileSize/vaddr、bssSize、userStackSize、flags。

---

## 6. 依赖关系

```
user (My_UI) ──▶ libwidget ──▶ libgui ──▶ guiapi syscall(int 0x80)
                                              │
kernel side ◀─────────────────────────────────┘
  wmsvc ──▶ window ──▶ vbe/vga
  install ──▶ fat32 ──▶ iso9660/file(drive abstract) ──▶ disk ──▶ ata/ahci/atapi
  graphic_main ──▶ vbe ──▶ bmp/file/elf
  syscall ──▶ wmsvc ; task ──▶ tss/paging/pmm
  keyboard/mouse ──▶ idt/isr
```

- 用户态依赖链完全受控：`My_UI` → `libwidget` → `libgui` → 系统调用，无 libc 依赖。
- `file.c` 依赖 `fat32`（优先）+ `iso9660`/GRUB 模块（后备）+ `disk/drive` 抽象。
- `vbe` 依赖 `pmm`（分配缓冲）、`string`（memcpy）、`bmp`（位图源）、`cjk16.bin`（中文字库）。
- 所有驱动经 `disk.c` 磁盘抽象层供文件系统使用。

---

## 7. 构建与运行方式

> **构建环境要求（WSL）**：`gcc(-m32)`、`ld(-m elf_i386)`、`nasm`、`grub-mkimage/grub-mkstandalone` + `/usr/lib/grub/i386-pc/` 模块、`python3(PIL)`、`xorriso`、`qemu-system-x86_64`。Windows 原生 `mingw32-make` 的 cmd shell 无法执行 UNIX 命令，故统一用 `wsl -- bash tools/build_wsl.sh` 委托。

### 一次完整构建 + 运行
```bash
# 1) 生成 CJK 字库（首次或换字体/分辨率/二值化参数时）
python tools/make_cjk.py              # 或 Windows 下用 make_cjk_gui.py 可视化调参

# 2) 全链构建内核 + ISO（Windows 侧统一入口）
wsl -- bash tools/build_wsl.sh        # 等价 wsl -- make all

# 3)（可选）编译用户态 GUI 程序
wsl -- bash -lc "cd ... && make program NAME=myapp"

# 4) 运行（CD 引导 QEMU）
wsl -- make run                       # 或 run-kvm / debug / run-headless / run-noaudio

# 5) 无头冒烟测试
wsl -- bash tools/wm_headless_test.sh
```

### build_wsl.sh 构建步骤（产物）
| 步骤 | 产出 |
|------|------|
| `make kernel.bin` | 内核二进制 |
| `grub-mkimage -O i386-pc-eltorito`（模块多，含 vbe vga） | `iso/boot/grub/eltorito.img`（CD 引导） |
| `grub-mkstandalone -O i386-pc`（memdisk 自包含） | `iso/grub/core.img`（≤491520B） |
| `python3 tools/gen_grub_hdd.py` | `iso/grub/hdd_mbr.bin`（512B MBR） |
| 拷 grub_cd.cfg、`cp -r system iso/system`、`xorriso -J` | `vortexos.iso` |

**三种引导配置**
- `grub_cd.cfg`：`insmod vbe/vga`、`gfxmode=1024x768x32,...,auto`、`gfxpayload=keep`、`multiboot2 /boot/kernel.bin` + 12 条 `module2`（把 font/cjk16/wallpaper/arrow/My_UI.elf/kernel.bin 等全树送入内存，作无存储驱动时的统一后备）。
- `grub_hdd_memdisk.cfg`：硬盘 memdisk 引导，gfxmode 回退链相同。
- `grub_hdd_embed.cfg`：embed 预留（串口配置）。

**QEMU 参数**：`-cdrom vortexos.iso -hda disk.img -m 256M -boot d -machine pc -cpu qemu64 -smp 2 -vga std`,
无头测试用 `-display none -serial stdio -device qemu-xhci -device usb-kbd -device usb-mouse`。

### 运行期顶层 Makefile 目标
| 目标 | 说明 |
|------|------|
| `all` / `clean` | 构建 ISO / 清理 |
| `run` / `run-kvm` / `run-noaudio` | 运行（KVM 加速 / 静音） |
| `debug` / `debug-kvm` | QEMU `-s -S` + `-d int` 记录 qemu.log |
| `run-headless` | 无显示 + 串口 |
| `program NAME=x` | 编译用户程序到 `system/programs/x.elf` |
| `grub-hdd` | 仅生成硬盘引导 core.img + hdd_mbr.bin |

---

## 8. 系统调用 ABI

用户 GUI 程序经 `int 0x80`：`EAX=调用号`，`EBX/ECX/EDX=参数`（身份映射，内核可直接访问用户指针）。

| 调用号 | 宏 | 参数 |
|--------|-----|------|
| 1 | `SYS_READ` | 基本 I/O |
| 2 | `SYS_WRITE` | 基本 I/O |
| 3 | `SYS_EXIT` | 退出 GUI 返回文本菜单 |
| 10 | `SYS_WM_CREATE` | ebx=`WmCreateArgs*` → 返回窗口索引(-1 失败) |
| 11 | `SYS_WM_SET_COLOR` | ebx=index, ecx=color |
| 12 | `SYS_WM_FILL_RECT` | ebx=index, ecx=cx\|cy<<16, edx=cw\|ch<<16 |
| 13 | `SYS_WM_DRAW_TEXT` | ebx=index, ecx=cx\|cy<<16, edx=text* |
| 14 | `SYS_WM_DRAW_LINE` | ebx=index, ecx=x0\|y0<<16, edx=x1\|y1<<16 |
| 15 | `SYS_WM_DRAW_LINE_RECT` | ebx=index, ecx=cx\|cy<<16, edx=cw\|ch<<16 |
| 16 | `SYS_WM_SET_TITLE` | ebx=index, ecx=title* |
| 17 | `SYS_WM_CLOSE` | ebx=index |
| 18 | `SYS_WM_FLUSH` | 合成+写 LFB+指针 |
| 19 | `SYS_WM_POLL` | ebx=`WmEvent*`：等待输入并回填 |
| 20 | `SYS_WM_GET_POS` | ebx=index, ecx=`WmPos*` |

鼠标按钮位：`GUI_MOUSE_LEFT 0x01 / RIGHT 0x02 / MIDDLE 0x04`。坐标打包为 `x | y<<16`（每轴 16 位）。

**调用链示例**：`guiFillRect → SYS_WM_FILL_RECT → wmsvcHandle → wmFillRect → vbe`。

---

## 附：相关文档
- `.trae/documents/window-system.md` — 窗口系统设计
- `.trae/documents/ui-designer.md` — 可视化 UI 设计器说明
- `.trae/documents/真机磁盘格式化与图形模式修复.md` — 真机兼容性修复记录