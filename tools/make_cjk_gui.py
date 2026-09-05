#!/usr/bin/env python3
# make_cjk_gui.py —— 可视化点阵字库编辑器（tkinter）
#
# 用途：浏览字体、实时调整各参数，即时预览点阵，并生成两份字库：
#   cjk16.bin  汉字 16×16（Unicode 索引，每条 36 字节：码点+32 字节点阵）
#   font.bin   ASCII/扩展 8×16（每条 17 字节：码+16 行）
#
# 运行：python3 make_cjk_gui.py
# 依赖：pillow + tkinter
# 说明：本工具在 Windows 本机运行，默认字体为桌面上的 Unifont（点阵风格，
#   中英文字形统一，中文 16×16 满格、拉丁 8×16 半宽）。可浏览换成任意字体。
#   index 仅对 .ttc 多字体集合有意义；Unifont 只有 index 0，加载失败会自动回退。
#   汉字满格 16×16 与 ASCII 同网格顶部对齐，垂直偏移由 vbe.c 的
#   CJK_GLYPH_UPDAWN 负责，可用“预览上移(px)”先试出再写进 vbe.c。
import os
import struct
import unicodedata

from PIL import Image, ImageDraw, ImageFont

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ImportError:
    import sys
    sys.stderr.write("需要 Python 的 Tk 支持（Windows 安装时勾选 tcl/tk，或 pip 无此包）。\n")
    sys.exit(1)

SIZE = 16          # 点阵尺寸 16×16
DEFAULT_RENDER = 112   # 大图渲染分辨率
DEFAULT_BINARIZE = 96  # 大图二值化阈值(越高笔画越细)
DEFAULT_THRESHOLD = 120 # 缩放后最终二值化阈值
ZOOM = 18          # 预览画布中每个点阵像素的边长(屏幕像素)
UNIFONT_WIN = r"C:/Users/Administrator/Desktop/其他/Sucai/Unifont-v17.0.05/Unifont-v17.0.05/unifont-17.0.05.otf"
CFG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "system", "font", "cjk16.bin")
FONT_BIN_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "..", "system", "font", "font.bin")
# ASCII 预览/生成范围：0x20 起，含 0x80~0xFF 扩展字符(控制符 0x00~0x1F 不生成)
ASCII_RANGE = list(range(0x20, 0x100))


class CjkFontGui:
    def __init__(self, root):
        self.root = root
        root.title("VortexOS 点阵字库编辑器")
        root.resizable(False, False)

        # 默认文本（可编辑）
        self.textVar = tk.StringVar(
            value="漩涡 系统 窗口 操作")
        self.fontPathVar = tk.StringVar(value=UNIFONT_WIN)
        self.fontIndexVar = tk.StringVar(value="0")       # Unifont 只有 index 0
        self.renderVar = tk.StringVar(value=str(DEFAULT_RENDER))
        self.binarizeVar = tk.StringVar(value=str(DEFAULT_BINARIZE))
        self.thresholdVar = tk.StringVar(value=str(DEFAULT_THRESHOLD))
        self.upVar = tk.IntVar(value=0)                    # 预览上移像素
        self.modeVar = tk.StringVar(value="cjk")           # cjk / ascii 预览模式
        self.charVar = tk.IntVar(value=0)                  # 当前预览索引

        # 缓存字体对象，避免每个字符重复加载
        self._fontCache = {}

        self.buildUi()
        self.refreshChars()
        self.redraw()

    # ---------------- UI ----------------
    def buildUi(self):
        frm = ttk.Frame(self.root, padding=8)
        frm.pack(fill=tk.BOTH, expand=True)

        # 字体区
        row = ttk.Frame(frm)
        row.pack(fill=tk.X)
        ttk.Label(row, text="字体文件:").pack(side=tk.LEFT)
        r = ttk.Frame(row)
        r.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 0))
        e = ttk.Entry(r, textvariable=self.fontPathVar)
        e.pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(r, text="浏览", command=self.chooseFont).pack(side=tk.LEFT, padx=(4, 0))

        row2 = ttk.Frame(frm)
        row2.pack(fill=tk.X, pady=(4, 0))
        ttk.Label(row2, text="字体内index(仅 .ttc 集合有效; Unifont=0):").pack(side=tk.LEFT)
        ttk.Entry(row2, textvariable=self.fontIndexVar, width=6).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(row2, text="   预览:").pack(side=tk.LEFT, padx=(8, 0))
        ttk.Radiobutton(row2, text="中文", value="cjk", variable=self.modeVar,
                        command=self.switchMode).pack(side=tk.LEFT)
        ttk.Radiobutton(row2, text="ASCII", value="ascii", variable=self.modeVar,
                        command=self.switchMode).pack(side=tk.LEFT)

        # 参数滑块区
        row3 = ttk.Frame(frm)
        row3.pack(fill=tk.X, pady=(6, 0))
        self.addParam(row3, "大图分辨率 RENDER", self.renderVar, 32, 256, 1)
        row4 = ttk.Frame(frm)
        row4.pack(fill=tk.X)
        self.addParam(row4, "大图阈值 BINARIZE", self.binarizeVar, 40, 255, 1)
        row5 = ttk.Frame(frm)
        row5.pack(fill=tk.X)
        self.addParam(row5, "最终阈值 THRESHOLD", self.thresholdVar, 40, 255, 1)
        row6 = ttk.Frame(frm)
        row6.pack(fill=tk.X)
        self.addParam(row6, "预览上移(px)≈CJK_GLYPH_UPDAWN", self.upVar, -8, 10, 1)

        # 画布
        self.canvas = tk.Canvas(frm, width=SIZE * ZOOM, height=SIZE * ZOOM,
                                bg="#ffffff", highlightthickness=1,
                                highlightbackground="#888888")
        self.canvas.pack(side=tk.LEFT, padx=(0, 8), pady=(6, 0))

        # 右侧：字符信息 + 导航
        right = ttk.Frame(frm)
        right.pack(side=tk.LEFT, fill=tk.Y, anchor="n", pady=(6, 0))
        self.charLabel = ttk.Label(right, text="", font=("TkDefaultFont", 12))
        self.charLabel.pack(anchor="w")
        nav = ttk.Frame(right)
        nav.pack(anchor="w", pady=(4, 0))
        ttk.Button(nav, text="◀", width=3, command=lambda: self.nav(-1)).pack(side=tk.LEFT)
        ttk.Button(nav, text="▶", width=3, command=lambda: self.nav(1)).pack(side=tk.LEFT, padx=(4, 0))

        # 文本区
        ttk.Label(frm, text="文本(生成这些字的点阵, 空格分隔连续字):").pack(anchor="w", pady=(6, 0))
        self.txt = tk.Text(frm, height=4, width=64, wrap=tk.WORD)
        self.txt.pack(fill=tk.X)
        self.txt.insert("1.0", self.textVar.get())

        # 按钮行
        btn = ttk.Frame(frm)
        btn.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(btn, text="生成 cjk16.bin + font.bin", command=self.generate).pack(side=tk.LEFT)
        ttk.Button(btn, text="预览当前字符", command=lambda: self.redraw()).pack(side=tk.LEFT, padx=(6, 0))

        # 事件刷新：滑杆即时预览
        self.upVar.trace_add("write", lambda *a: self.redraw())

    def addParam(self, parent, label, var, lo, hi, step):
        """一行：label + Entry + Scale(横向)，Scale 拖动实时触发。"""
        frame = ttk.Frame(parent)
        frame.pack(fill=tk.X, pady=(2, 0))
        ttk.Label(frame, text=label + ":").pack(side=tk.LEFT)
        ent = ttk.Entry(frame, textvariable=var, width=5)
        ent.pack(side=tk.LEFT, padx=(4, 0))
        # 用 StringVar 包一个临时 Scale；结束时写入 var
        sc = ttk.Scale(frame, from_=lo, to=hi, value=float(var.get()), orient=tk.HORIZONTAL,
                       command=lambda v, vv=var: vv.set(str(int(float(v)))))
        sc.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0))
        sc.bind("<ButtonRelease-1>", lambda e: self.redraw())

    # ---------------- 数据 ----------------
    def collectedChars(self):
        return collect_chars(self.textVar.get() or "")

    def currentChar(self):
        chars = self.collectedChars()
        if not chars:
            return None
        return chars[self.charVar.get() % len(chars)]

    def getFont(self, render):
        path = self.fontPathVar.get().strip()
        key = (path, render)
        if key in self._fontCache:
            return self._fontCache[key]
        try:
            idx = int(self.fontIndexVar.get() or 0)
        except ValueError:
            idx = 0
        # 先按用户指定 index 加载；若该 index 无效(如 Unifont 只有 index 0)，
        # 自动回退到 index 0 并更新输入框，避免渲染直接失败。
        try:
            f = ImageFont.truetype(path, render, index=idx)
        except (OSError, ValueError):
            try:
                f = ImageFont.truetype(path, render, index=0)
            except (OSError, ValueError):
                raise RuntimeError("无法用该字体加载字形(可能不是字体文件)")
            self.fontIndexVar.set("0")
        self._fontCache[key] = f
        return f

    def rasterize(self, ch, render, binarize, threshold):
        """同 make_cjk.py 的 rasterize，改为参数化。"""
        try:
            font = self.getFont(render)
        except Exception:
            return None
        img = Image.new("L", (render, render), 0)
        d = ImageDraw.Draw(img)
        bbox = d.textbbox((0, 0), ch, font=font)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        ox = max(0, (render - w) // 2 - bbox[0])
        oy = max(0, (render - h) // 2 - bbox[1])
        d.text((ox, oy), ch, font=font, fill=255)
        big = img.point(lambda v: 255 if v > binarize else 0)
        small = big.resize((SIZE, SIZE), Image.LANCZOS)
        px = list(small.getdata())
        return [[px[r * SIZE + c] > threshold for c in range(SIZE)] for r in range(SIZE)]

    def rasterizeAscii(self, code, render, binarize, threshold):
        """ASCII/扩展码 → 8×16 二值点阵(16 行 × 8 列)。Unifont 拉丁字形
        占 8×16 半宽单元：画布 8k×16k(k=render/16)，字形自左上铺开。"""
        try:
            font = self.getFont(render)
        except Exception:
            return None
        k = render // 16
        img = Image.new("L", (8 * k, 16 * k), 0)
        d = ImageDraw.Draw(img)
        d.text((0, 0), chr(code), font=font, fill=255)
        big = img.point(lambda v: 255 if v > binarize else 0)
        small = big.resize((8, SIZE), Image.LANCZOS)
        px = list(small.getdata())
        return [[px[r * 8 + c] > threshold for c in range(8)] for r in range(SIZE)]

    # ---------------- 视图 ----------------
    def switchMode(self):
        self.charVar.set(0)
        self.refreshChars()
        self.redraw()

    def refreshChars(self):
        if self.modeVar.get() == "ascii":
            code = ASCII_RANGE[self.charVar.get() % len(ASCII_RANGE)]
            name = unicodedata.name(chr(code), "")
            if code < 0x80 and 0x20 <= code < 0x7F:
                disp = "'%s'" % chr(code)
            else:
                disp = ""
            self.charLabel.config(text="ASCII 0x%02X %s %s" % (code, disp, name))
            return
        chars = self.collectedChars()
        if not chars:
            self.charLabel.config(text="(无可生成字符)")
            return
        self.charVar.set(self.charVar.get() % len(chars))
        self.charLabel.config(text="%c  U+%04X  %s"
                             % (chars[self.charVar.get()],
                                ord(chars[self.charVar.get()]),
                                unicodedata.name(chars[self.charVar.get()], "")))

    def nav(self, delta):
        if self.modeVar.get() == "ascii":
            self.charVar.set((self.charVar.get() + delta) % len(ASCII_RANGE))
            self.refreshChars()
            self.redraw()
            return
        chars = self.collectedChars()
        if not chars:
            return
        self.charVar.set((self.charVar.get() + delta) % len(chars))
        self.refreshChars()
        self.redraw()

    def redraw(self):
        self.canvas.delete("all")
        try:
            render = max(8, int(self.renderVar.get()))
            binarize = int(self.binarizeVar.get())
            threshold = int(self.thresholdVar.get())
        except ValueError:
            return

        if self.modeVar.get() == "ascii":
            code = ASCII_RANGE[self.charVar.get() % len(ASCII_RANGE)]
            glyph = self.rasterizeAscii(code, render, binarize, threshold)
            if glyph is None:
                self.charLabel.config(text="字体加载失败，请检查字体文件路径与 index")
                return
            # ASCII 8 列宽，画在左半；右侧画一条浅色分界参考线
            self.canvas.create_line(8 * ZOOM, 0, 8 * ZOOM, SIZE * ZOOM,
                                    fill="#dddddd")
            for r in range(SIZE):
                for c in range(8):
                    if not glyph[r][c]:
                        continue
                    x0, y0 = c * ZOOM, r * ZOOM
                    self.canvas.create_rectangle(x0 + 1, y0 + 1,
                                                 x0 + ZOOM - 1, y0 + ZOOM - 1,
                                                 fill="#000000", outline="#555555")
            return

        ch = self.currentChar()
        if not ch:
            return
        glyph = self.rasterize(ch, render, binarize, threshold)
        if glyph is None:
            self.charLabel.config(text="字体加载失败，请检查字体文件路径与 index")
            return
        up = self.upVar.get()

        # 画放大点阵；up>0 表示字形整体上移(模拟 CJK_GLYPH_UPDAWN)
        for r in range(SIZE):
            for c in range(SIZE):
                if not glyph[r][c]:
                    continue
                yy = (r - up) * ZOOM
                if yy < -ZOOM or yy >= SIZE * ZOOM:
                    continue
                x0, y0 = c * ZOOM, yy
                self.canvas.create_rectangle(x0 + 1, y0 + 1,
                                             x0 + ZOOM - 1, y0 + ZOOM - 1,
                                             fill="#000000", outline="#555555")

    # ---------------- 动作 ----------------
    def chooseFont(self):
        path = filedialog.askopenfilename(
            filetypes=[("字体", "*.ttc *.ttf *.otf"), ("所有文件", "*.*")])
        if path:
            self.fontPathVar.set(path)
            self.redraw()

    def syncText(self):
        self.textVar.set(self.txt.get("1.0", "end").rstrip("\n"))
        self.charVar.set(0)
        self.refreshChars()

    def generate(self):
        self.syncText()
        path = self.fontPathVar.get().strip()
        if not path:
            messagebox.showwarning("提示", "请先选择一个字体文件。")
            return
        try:
            render = int(self.renderVar.get())
            binarize = int(self.binarizeVar.get())
            threshold = int(self.thresholdVar.get())
        except ValueError:
            messagebox.showerror("错误", "RENDER/BINARIZE/THRESHOLD 必须是整数。")
            return

        # 1) 汉字字库 cjk16.bin
        chars = self.collectedChars()
        if not chars:
            messagebox.showwarning("提示", "文本中没有码点 >= 0x80 的中文字符。")
            return

        blob = bytearray()
        failed = []
        for ch in chars:
            glyph = self.rasterize(ch, render, binarize, threshold)
            if glyph is None:
                failed.append(ch)
                continue
            blob += struct.pack("<I", ord(ch))
            blob += pack(glyph)

        if failed:
            messagebox.showwarning("部分失败", "以下字符无法用该字体渲染(可能缺字或字体路径错误):\n" +
                                   "".join(failed))

        os.makedirs(os.path.dirname(CFG_PATH) or ".", exist_ok=True)
        with open(CFG_PATH, "wb") as f:
            f.write(bytes(blob))

        # 2) ASCII 字库 font.bin（0x20~0xFF，每条 17 字节：码 + 16 行）
        ab = bytearray()
        for code in ASCII_RANGE:
            glyph = self.rasterizeAscii(code, render, binarize, threshold)
            if glyph is None:
                continue
            ab.append(code)
            ab += packAscii(glyph)
        with open(FONT_BIN_PATH, "wb") as f:
            f.write(bytes(ab))

        messagebox.showinfo("完成",
                            "%d 字 -> %s (%d 字节)\n"
                            "%d 字符 -> %s (%d 字节)\n"
                            "建议的 CJK_GLYPH_UPDAWN=%d"
                            % (len(chars), CFG_PATH, len(blob),
                               len(ASCII_RANGE), FONT_BIN_PATH, len(ab),
                               self.upVar.get()))


# ---------- 复用 make_cjk 的工具函数 ----------
def collect_chars(text):
    chars = []
    for ch in text:
        cp = ord(ch)
        if cp < 0x80:
            continue
        if ch not in chars:      # 用字符去重(列表存的是字符, 不能拿码点整数比较)
            chars.append(ch)
    return sorted(chars, key=ord)


def pack(glyph):
    out = bytearray()
    for row in glyph:
        hi = lo = 0
        for c in range(8):
            if row[c]:
                hi |= 1 << (7 - c)
        for c in range(8, 16):
            if row[c]:
                lo |= 1 << (15 - c)
        out.append(hi)
        out.append(lo)
    return bytes(out)


def packAscii(glyph):
    """8 列宽字形 → 每行 1 字节(位 7 为最左列)，共 16 字节。"""
    out = bytearray()
    for row in glyph:
        byte = 0
        for c in range(8):
            if row[c]:
                byte |= 1 << (7 - c)
        out.append(byte)
    return bytes(out)


def main():
    root = tk.Tk()
    CjkFontGui(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())