# 实现窗口系统（首个迭代：窗口管理器 + 合成 + 拖拽/聚焦/关闭/最小化）

## Context（背景）

当前 `graphic_main()`（[kernel.c](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/kernel/kernel.c#L544-L622)）只做"静态背景 + 鼠标指针恢复"：把壁纸/任务栏画进一块 `shadow` RAM 缓冲，指针移动时从 shadow 恢复旧区域再画新指针。没有窗口、没有层叠、没有命中。为支撑"图形化功能"（README 目标），需要真正的窗口系统。

**目标（本次迭代，由用户选定）：**
- 合成方案：**全帧重合成**——每次事件把 背景 + 所有窗口(自下而上) + 指针 重新画进 `shadow`，再整体拷回 LFB。
- 功能范围：**可拖拽 + 点击聚焦 的演示窗口**，加上**标题栏 关闭(X) / 最小化(_) 按钮**；ESC 返回文本主菜单。

**约束（沿用工程约定）：**
- VBE 单一当前绘制色模型：所有绘图函数不接收颜色参数，用 `vbeSetColor()` 设置后绘制。
- QEMU bochs VBE 的 LFB 可写不可回读 → 合成始终走 RAM(`shadow`)，最后整帧写 LFB。
- 绘制函数（[vbe.c](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/lib/stdio/vbe.c)）：`vbeDrawFillRect`/`vbeDrawLineRect`/`vbeDrawLine`/`vbeDrawBitmap`/`vbeDrawStringCJK` 均已可用；`vbeBeginRamFrame(buf)`/`vbeEndRamFrame()` 把目标重定向到 RAM 缓冲。

---

## 新增文件

### 1) `src/include/wm/window.h`（对外 API + 类型）
```c
#define WM_TITLEBAR_H 20
#define WM_ICON_W 18
#define WM_MAX_WINDOWS 16

typedef struct Window Window;
typedef void (*WindowDrawFn)(Window* w);   // 可选：自定义窗口内容(用绝对屏幕坐标)

typedef struct Window {
    int x, y, w, h;
    const char* title;
    uint32_t titleColor, bodyColor;        // 标题栏/客户区颜色
    bool visible;
    bool minimized;
    WindowDrawFn draw;                     // 可为 NULL(默认画纯色客户区)
} Window;

int   wmInit(uint32_t* bg, int width, int height);   // bg: 已合成的背景帧缓冲(壁纸+任务栏)
int   wmCreate(const char* title, int x, int y, int w, int h,
               uint32_t titleColor, uint32_t bodyColor, WindowDrawFn draw);
void  wmComposite(uint32_t* fb);          // 合成：bg拷贝 + 窗口(自下而上) + 写入 fb 目标
int   wmHitTest(int x, int y);            // 返回包含该点的最上层窗口索引，或 -1
void  wmFocus(int index);                 // 置顶聚焦(index 移到最后)
void  wmMove(int index, int x, int y);    // 移动窗口(带屏幕 clamp)
void  wmClose(int index);
void  wmMinimize(int index);
void  wmRestore(int index);
int   wmWindowCount(void);
```
> 在窗口标题栏点 X 命中 `wmClose`，点 `_` 命中 `wmMinimize`；被最小化的窗口在 `wmComposite` 中跳过。

### 2) `src/kernel/wm/window.c`（实现）
- 静态状态：`Window gWindows[WM_MAX_WINDOWS]`、`gWmCount`、`gBg`(每次合成的背景源)、屏幕宽高。
- `wmComposite(fb)`：
  1. `memcpy(fb, gBg, (size_t)wmW * wmH * 4)`（fb 是 shadow，纯内存拷贝）。
  2. `vbeBeginRamFrame(fb)` → 按数组顺序(数组尾部=最上层)逐个画 `visible && !minimized` 的窗口：
     - 边框：`vbeDrawLineRect(x,y,w,h)`；
     - 标题栏：`vbeDrawFillRect(x, y, w, WM_TITLEBAR_H)`；
     - 标题文本：`vbeDrawStringCJK(x+6, y+4, title)`；
     - 最小化按钮：右上角 `vbeDrawFillRect`，画一条 `_`；
     - 关闭按钮：右上角靠最外 `vbeDrawFillRect`，画一个 `x`；
     - 客户区：`vbeDrawFillRect(x, y+WM_TITLEBAR_H, w, h-WM_TITLEBAR_H)`；若 `draw` 非空则调用（此时窗口已激活为 fb 目标，用绝对坐标继续绘制）。
  3. `vbeEndRamFrame()`。
- `wmHitTest(x,y)`：从数组尾部向前找第一个 `visible&&!minimized` 且矩形含点的索引。
- `wmFocus(i)`：把该窗口移到数组尾部；`wmClose` 置 `visible=false`；`wmMinimize` 置 `minimized=true`。
- `wmMove`：更新 x,y（clamp 到屏幕内、y 不小于 0）。

---

## 修改文件

### 3) `src/kernel/kernel.c` — `graphic_main()` 改为 WM 驱动循环
保留：VBE 设置、壁纸/任务栏/箭头位图加载、`shadow` 分配（现在作为**合成缓冲**而非静态快照）、`drawMousePointer`/`POINTER_RECT_W/H`。

改动点：
1. **建背景帧缓冲** `bg`（同 `shadow` 尺寸的 RAM）：`vbeBeginRamFrame(bg)` → 画壁纸 + 任务栏 → `vbeEndRamFrame()`，随后可 `free(wallpaper.pixels/taskbar.pixels)`；调 `wmInit(bg, vbeWidth, vbeHeight)`。
2. **创建演示窗口**（3 个，不同颜色/标题），例如：
   ```c
   wmCreate("Terminal", 60, 70, 380, 250, Color(60,60,160), Color(24,24,24), termDraw);
   wmCreate("Notepad",  450, 120, 380, 260, Color(40,120,60), Color(250,250,250), NULL);
   wmCreate("About",    200, 280, 340, 180, Color(140,90,40), Color(235,235,235), aboutDraw);
   ```
   `termDraw`/`aboutDraw` 用 `vbeSetColor + vbeDrawFillRect/vbeDrawStringCJK` 画几行内容演示自定义内容。
3. **事件循环重写**（替代原"恢复旧指针"逻辑）：
   ```
   mouseSetBounds/SetPosition 居中；sti；
   for(;;){
       记录上一次按钮 prevButtons、拖拽索引 dragWm=-1；
       等待 mouseHasEvent() 或 keyboardHasChar()；
       nx,ny = mouseGetX/Y；btn = mouseGetButtons()；
       // 左键按下沿：命中→聚焦到最上层；若点标题栏则开始拖拽并记录偏移；
       //      若点 X/_ 按钮则 wmClose/wmMinimize；
       // 移动中若 dragWm>=0：wmMove(dragWm, nx-offx, ny-offy)；
       // 左键释放：dragWm=-1；
       // 任意上述改变都需要：
       wmComposite(shadow);
       vbeBeginRamFrame(shadow); drawMousePointer(nx,ny); vbeEndRamFrame();
       memcpy((void*)gVbeInfo.lfbAddr, shadow, shadowPix*4);
       // ESC：break
   }
   ```
4. **ESC 退出**：循环内 `keyboardHasChar()`，若 `c==KEY_ESC` → `break`；跳出后 `vbeDisable(); vgaClear(); drawMainMenu();` 返回文本主菜单（注意 VGA 字体此前已上传，无需重载）。
> 指针恢复不再需要：因为每帧整体重合成 + 整帧写 LFB，指针与窗口天然正确重叠，消除原 `blitShadowToLfb` 恢复逻辑。

**鼠标按钮 API**（[mouse.h](file:///c:/Users/Administrator/Documents/OperatingSystem/vortex-os/src/drivers/mouse.h)）：`mouseGetButtons()`/`mouseHasEvent()`/`mouseGetX/Y()`。`MOUSE_LEFT_BUTTON=0x01` 已在头文件中定义。

---

## 复用点（不要重造）
- 背景合成画壁纸/任务栏：复用 `vbeDrawBitmap`。
- 窗口边框/标题栏/按钮：`vbeDrawFillRect` / `vbeDrawLineRect`。
- 标题与中文文本：`vbeDrawStringCJK`。
- 指针绘制：现有 `drawMousePointer` + `gMouseArrow`。
- 整帧写 LFB：现有 `memcpy(lfb, shadow, shadowPix*4)`。
- RAM 目标重定向：`vbeBeginRamFrame` / `vbeEndRamFrame`。

---

## 验证（端到端）
1. 构建：`wsl -- bash tools/build_wsl.sh`（Makefile 用 `find src -name "*.c"` 自动纳入新文件）。
2. 交互验证（有显示）：`make run` → 主菜单选 `Graphic` → 应看到 3 个层叠的演示窗口（标题栏+颜色客户区+X/_按钮）：
   - 用鼠标**拖动**标题栏移动窗口；
   - **点击不同窗口**使其浮到最上层；
   - 点击 **X** 关闭、**_** 最小化（最小化后该窗口隐藏）；
   - 按 **ESC** 返回文本主菜单，再进 Graphic 正常。
3. 无头 sanity（可选）：用 `tools/repro.sh`（进入 Graphic）后检查串口无 `<<< EXCEPTION`；可在 `wm` 内加 `serialPutStr` 打印 `[WM] count=N` 辅助确认 3 个窗口已建。
4. 回归：确认无窗口模式下（不选 Graphic）文本主菜单/Shell 不受影响。