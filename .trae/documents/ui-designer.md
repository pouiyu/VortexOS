# UI 设计器：拖动组件 → 生成可编译的 VortexOS GUI 程序

## Context（背景/目标）

VortexOS 目前只有一个手写 GUI 示例程序 [gui_demo.c](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/user/gui_demo.c)：`_start` 里逐个调用 `guiFillRect/guiDrawText` 画按钮、再用 `guiPoll` + 手写坐标数学做命中检测。开发一个带界面的 UI 时每次都要手写这堆样板很累。

目标：做一个 **Windows 可视化 UI 设计器**（拖放 Button/Label/TextBox/CheckBox/Line/Rect 6 种组件、改属性），自动生成**可编译的 C 程序文件**。生成代码跑在现有用户态 GUI 架构上——**不改内核**。

用户已确认：
- 组件集：Button、Label、TextBox（文本框）、CheckBox、Line、Rect。
- 生成结构：内置一个 **libwidget 运行时库**（构建在 [libgui](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/include/gui/libgui.h) 之上），生成程序 = 单文件 `.c`（控件表 + `_start` + 事件循环）引用 libwidget。
- 编译链路：设计器**仅生成 .c**；编译/打包沿用现有 [compile_tool.ps1](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/tools/compile_tool.ps1) 或 `build_wsl.sh`（词条需把 libwidget 纳入链接）。
- 控制台程序（`jumpToUserConsole`、非图形文本 I/O）**本轮不做**，仅标注为后续任务。

## 关键事实（来自现有代码，直接复用）

- 客户区坐标系：`guiDrawText/guiFillRect` 等以**客户区左上角为 (0,0)**；标题栏高 `WM_TITLEBAR_H=20`（[window.h](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/include/wm/window.h#L8)）。
- 事件：[WmEvent](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/include/wm/guiapi.h#L44)：`mouseX/mouseY`=屏幕绝对坐标，`buttons` 位（`GUI_MOUSE_LEFT`），`key`=ASCII（来自 `keyboardGetChar`）。普通可打印键进来就是字符（`'A'`=65），退格为 `KEY_BACKSPACE(14)/'\b'`，`KEY_ESC` 退出。
- 屏幕→客户区换算：`mx = ev.mouseX - wx`，`my = ev.mouseY - wy - WM_TITLEBAR_H`（[gui_demo.c](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/user/gui_demo.c#L47)）。
- 单个字体色模型：先 `guiSetColor` 再画（[libgui.h](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/include/gui/libgui.h#L27)）。
- 可用原语：`guiFillRect` 填充、`guiDrawText` 文本(CJK)、`guiDrawLine` 直线、`guiDrawLineRect` 线框。
- 构建：`USER_CFLAGS` + `ld -m elf_i386 -T user.ld <prog.o> libgui.o`（[Makefile](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/Makefile#L28)），链接地址 0x400000，入口 `_start`。

## 要新增/修改的文件

### 1) libwidget 运行时库（用户态，E 构建在 libgui 之上）
- **新增** `src/include/gui/libwidget.h`
- **新增** `src/user/libwidget.c`

统一控件模型：

```c
typedef enum { WD_BUTTON=0, WD_LABEL=1, WD_TEXTBOX=2, WD_CHECKBOX=3,
               WD_LINE=4, WD_RECT=5 } WdType;

typedef struct {
    WdType type;
    int x, y, w, h;          /* 客户区相对坐标 */
    const char* label;       /* 显示的文本 */
    uint32_t fg, bg, border; /* 前景/背景/边框色 */
    /* 运行时状态 */
    int down;                /* Button: 按下=1 */
    char* buf; int cap;      /* TextBox: 可写输入缓冲 + 容量(程序内静态数组) */
    int len;                 /* TextBox 当前字节数 */
    int checked;             /* CheckBox: 是否勾选 */
} Widget;
```

API：
```c
void wdInit(Widget* ws, int n);                         /* 复位 inten/文本状态(从 label 拷贝到 buf) */
void wdDraw(int win, Widget* ws, int n);                /* 依序绘制全部控件(每种 type 一个分支) */
int  wdHit(const Widget* ws, int n, int mx, int my);    /* 返回命中的控件 index, 未命中 -1(纯几何) */
int  wdPress(int win, Widget* ws, int n, int idx, int down); /* 按钮按下/checkbox 切换, 内部重绘该控件 */
int  wdHandleKey(const Widget*, int win, Widget* ws, int n, int* focus, char key); /* 文本框进字/退格, 换色 */
```

绘制约定（每种 type 一个 `if/switch`，全部用 libgui 原语）：
- **Button**：填充 `bg`，失去按下换 `fg`；白字 `label` 居中（用 `guiDrawText`）。
- **Label**：`guiDrawText(label, x, y)`。
- **TextBox**：白底 + `border` 线框(`guiDrawLineRect`) + `buf[0..len]` 文本；空可画占位。
- **CheckBox**：小方块(`guiFillRect` + 边框) + `[x]`/`[ ]` 或无勾时画对勾(`guiDrawLine`) + `label`。
- **Line**：按长边方向用 `guiDrawLine` 画水平/垂直分隔线。
- **Rect**：`guiDrawLineRect`。

注意：用户程序**无堆**，TextBox 输入缓冲由生成代码声明为静态数组（Windows开放不了 UIText），`widget.buf` 指向它。

### 2) Makefile + compile_tool 纳入 libwidget
- **修改** `Makefile`：把 `libwidget.o` 加入用户程序链接 recipe（与 `libgui.o` 并列，先于目标 `.o`）。
- **修改** `tools/compile_tool.ps1`：`Compile-Selected` 的 bash 命令里在编 libgui.c 后追加编并链 `libwidget.c`（生成的程序依赖它）。

### 3) 可视化设计器
- **新增** `tools/ui_designer.ps1`（Windows PowerShell WinForms，编码 UTF-8 with BOM，零依赖）。

布局：
- **左列**：6 个"添加组件"按钮 + 一个控件列表（DataGridView 两列：名称|类型），点击画布上的控件或列表选中某控件。
- **中间**：画布 `Panel`（`Paint` 事件渲染窗口客户区矩形 + 按控件坐标/大小/文本画预览图）。用 `MouseDown/Move/Up`：拖动移动选中控件；在右下角拖拽改大小；右键删除。
- **右列**：属性编辑表（DataGridView，两列 属性|值），编辑选中控件的 text/fg/bg/border/x/y/w/h(文本框额外 cap；checkbox 初始 checked)。
- **顶部**：窗口属性（标题、宽、高、背景色）+ **生成** 按钮。生成后弹出保存对话框。

生成逻辑：把控件表序列化成 C 初始化器数组 `ws[i]`，为每个 TextBox 生成 `static char tx_buf[N];` 并把 `buf` 指向它；拼出完整 `_start`（创建窗口 → `wdInit` → `focus=-1` → 画 → 事件循环：`guiPoll` → ESC退出 → 换算客户区 → 左键 `wdHit` 设 focus + `wdPress` → `wdHandleKey` → 有变更 `wdDraw`+`guiFlush`）。保存到 `src/user/<名>.c`。

PS 技术要点：用 `System.Windows.Forms` + `Panel.OnPaint`；颜色存 `System.Drawing.Color`，生成时转回 `guiRgb(r,g,b)`；控件对象用 PowerShell `class WidItem`（公开标量属性，便于 DataGridView 编辑与代码生成）。

## 生成程序效果（示例，可手工先敲一个 `hello_ui.c` 验证库）

```c
#include <gui/libwidget.h>
static char tb1_buf[32];
static Widget ws[] = {
    { WD_LABEL,   20, 20, 0, 0,  "Name:",  guiRgb(40,40,40),0,0, 0,0,0,0, 0 },
    { WD_TEXTBOX, 90, 20, 200,30, "",      guiRgb(0,0,0),   guiRgb(255,255,255), guiRgb(120,120,120), 0, tb1_buf,32,0, 0 },
    { WD_BUTTON, 120, 70, 120,40, "OK",    guiRgb(255,255,255), guiRgb(60,110,200), guiRgb(20,40,90), 0,0,0,0, 0 },
};
void _start(void){
    int win = guiCreateWindow("My UI", 120,90, 360, 160, guiRgb(60,80,170),
                              guiRgb(240,240,240), WM_STYLE_DEFAULT);
    if(win<0) guiExit();
    wdInit(ws, sizeof(ws)/sizeof(ws[0]));
    int focus=-1;
    wdDraw(win, ws, 3); guiFlush();
    for(;;){
        WmEvent ev; guiPoll(&ev);
        if(ev.key==KEY_ESC) break;
        int wx,wy; guiGetWinPos(win,&wx,&wy);
        int mx=ev.mouseX-wx, my=ev.mouseY-wy-WM_TITLEBAR_H;
        if(ev.buttons & GUI_MOUSE_LEFT){
            int idx=wdHit(ws,3,mx,my);
            if(idx>=0){ focus=idx; wdPress(win,ws,3,idx, (ev.buttons&GUI_MOUSE_LEFT)?1:0); }
            else if(ev.leftDown==0) focus=-1;
        }
        if(wdHandleKey(win,ws,3,&focus,ev.key)){ wdDraw(win,ws,3); guiFlush(); }
    }
    guiExit();
}
```
（`wdHandleKey` 返回是否需要重绘；`wdPressed/down` 状态在合成前需一次重绘——由 `wdHandleKey` 与 `wdPress` 内部对已画控件再画该控件，设计器生成的事件循环负责触发 `guiFlush`。）

## 验证

1. **库编译**：WSL 里用 `USER_CFLAGS` 单独编 `libwidget.c`（语法/依赖无误），再手工按上例写 `src/user/hello_ui.c` 并 `gcc ... -c` + `ld ... hello_ui.o libwidget.o libgui.o` 生成功 `system/programs/hello_ui.elf`。
2. **实机运行**：改 [kernel.c](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/kernel/kernel.c) 的 graphic_main 临时指向 `hello_ui.elf`；`wsl -- bash tools/build_wsl.sh`；无头测试 `wsl -- bash tools/wm_headless_test.sh 6` 应见 `[WM] window created` 且窗口客户区渲染出控件；再 screendump 目测 Button/TextBox/CheckBox 外观。验证后还原为 `gui_demo`（或保留一个进入项）。
3. **再确认**：文本框聚焦后按键（ASCII）能 append、退格能删、CheckBox 点击能切换、Button 按下变色。
4. **设计器可运行**：`powershell -STA -File tools\ui_designer.ps1` 语法/启动正常，放置 6 类组件、改属性、生成 .c，生成的 .c 能用第 1 步的命令编译通过。

## 后续（本轮不做这条，仅标注）

控制台用户程序：新增 `jumpToUserConsole(entry)`（复用用户段/栈切换逻辑，但不初始化图形）+ 用已存在的 `SYS_WRITE/SYS_READ` 做 putc/gets，并从菜单/Shell 增加加载入口。与 GUI 设计器无关，另开任务。