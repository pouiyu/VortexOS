#!/usr/bin/env python3
# fontEditor.py
# 8x16 点阵字体编辑器，输出 font.bin。
#
# 文件格式（每字符 17 字节）：
#   [0]        字符编码（如 'A' = 65）
#   [1..16]    16 字节图案，每一字节对应一行，共 16 行；
#              每字节的 8 位对应 8 列（位方向见“位方向”选项）。
#
# 用法：
#   python3 fontEditor.py            # 从 font/font.bin 加载（若存在）
#   python3 fontEditor.py 你的文件   # 从指定文件加载

import os
import struct
import sys

try:
    import tkinter as tk
    from tkinter import messagebox, filedialog
    from tkinter.simpledialog import askinteger
except ImportError:
    sys.stderr.write("需要 Python 的 Tk 支持（Ubuntu: sudo apt install python3-tk）。\n")
    sys.exit(1)

WIDTH = 8       # 每字符 8 列
HEIGHT = 16     # 每字符 16 行
ZOOM = 24       # 编辑画布中每个像素的边长(像素)
LOADED_PATH = None


class FontEditor:
    def __init__(self, root):
        self.root = root
        root.title("VortexOS 8x16 字体编辑器")
        root.resizable(False, False)

        # 字符码 -> 16 字节位图；仅保存本会话新定义或修改过的字符
        self.fontData = {}
        # 已加载的需要保存的字符码（用于区分"加载后未改动"是否要写回）
        self.currentCode = None

        self.buildUi()
        self.loadFontFile(LOADED_PATH if LOADED_PATH else self.defaultPath())
        self.drawChars()

    def defaultPath(self):
        # 默认指向工程内的 font/font.bin
        base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        return os.path.join(base, "font", "font.bin")

    # ---------------- UI ----------------
    def buildUi(self):
        top = tk.Frame(self.root)
        top.pack(side=tk.TOP, fill=tk.X, padx=6, pady=6)

        tk.Label(top, text="字符码:").pack(side=tk.LEFT)
        self.codeEntry = tk.Entry(top, width=6)
        self.codeEntry.pack(side=tk.LEFT)
        self.codeEntry.insert(0, "65")
        self.importBtn = tk.Button(top, text="导入 .fon/.fnt…", command=self.importFontFile)
        self.importBtn.pack(side=tk.LEFT, padx=(4, 8))
        self.loadCodeBtn = tk.Button(top, text="加载字符", command=self.loadCodeById)
        self.loadCodeBtn.pack(side=tk.LEFT, padx=4)
        self.clearCharBtn = tk.Button(top, text="清空字符", command=self.clearChar)
        self.clearCharBtn.pack(side=tk.LEFT, padx=4)

        tk.Label(top, text="位方向:").pack(side=tk.LEFT, padx=(16, 0))
        self.lsbLeft = tk.BooleanVar(value=False)
        tk.Radiobutton(top, text="MSB=左(PSF)", variable=self.lsbLeft,
                       value=False).pack(side=tk.LEFT)
        tk.Radiobutton(top, text="LSB=左", variable=self.lsbLeft,
                       value=True).pack(side=tk.LEFT)

        mid = tk.Frame(self.root)
        mid.pack(side=tk.LEFT, fill=tk.Y, padx=6, pady=6)

        tk.Label(mid, text="选择字符(可滚动)").pack(side=tk.TOP)
        self.charList = tk.Frame(mid)
        self.charList.pack(side=tk.TOP)
        self.charButtons = []      # (code, button, label)
        self.loadedCodes = set()   # 有定义(来自文件或新建)的码

        self.canvas = tk.Canvas(self.root, width=WIDTH * ZOOM, height=HEIGHT * ZOOM,
                                bg="white", highlightthickness=1,
                                highlightbackground="black")
        self.canvas.pack(side=tk.LEFT, padx=6, pady=6)
        self.canvas.bind("<Button-1>", lambda e: self.paint(e, 1))
        self.canvas.bind("<B1-Motion>", lambda e: self.paint(e, 1))
        self.canvas.bind("<Button-3>", lambda e: self.paint(e, 0))
        self.canvas.bind("<B3-Motion>", lambda e: self.paint(e, 0))

        bottom = tk.Frame(self.root)
        bottom.pack(side=tk.BOTTOM, fill=tk.X, padx=6, pady=6)
        self.status = tk.Label(bottom, text="", anchor="w")
        self.status.pack(side=tk.LEFT)
        tk.Button(bottom, text="另存为…", command=self.saveFontAs).pack(side=tk.RIGHT)
        tk.Button(bottom, text="保存", command=self.saveFont).pack(side=tk.RIGHT, padx=4)

        self.renderCanvas()
        self.updateStatus()

    # ---------------- 字符表 ----------------
    def buildCharGrid(self):
        for w in self.charList.winfo_children():
            w.destroy()
        buttons = []
        # 展示可打印 ASCII 32..126
        codes = list(range(32, 127))
        for i, c in enumerate(codes):
            b = tk.Button(self.charList, text=chr(c), width=2,
                          command=lambda code=c: self.selectCode(code))
            b.grid(row=i // 16, column=i % 16, padx=1, pady=1)
            buttons.append((c, b))
        self.charButtons = buttons

    def refreshCharGrid(self):
        if not self.charButtons:
            return
        for code, b in self.charButtons:
            defined = code in self.fontData
            selected = (code == self.currentCode)
            fg = "#ffffff" if defined else "#000000"
            bg = "#2255aa" if selected else ("#dddddd" if defined else "#f0f0f0")
            b.config(fg=fg, bg=bg)

    def drawChars(self):
        self.buildCharGrid()
        self.refreshCharGrid()

    def selectCode(self, code):
        self.currentCode = code
        self.codeEntry.delete(0, tk.END)
        self.codeEntry.insert(0, str(code))
        self.renderCanvas()
        self.refreshCharGrid()
        self.updateStatus()

    def loadCodeById(self):
        try:
            code = int(self.codeEntry.get().strip())
            if not (0 <= code <= 255):
                messagebox.showerror("错误", "字符码须在 0..255 之间")
                return
            self.selectCode(code)
        except ValueError:
            messagebox.showerror("错误", "请输入 0..255 的整数编码")

    # ---------------- 位图存取 ----------------
    def getBitmap(self, code):
        if code not in self.fontData:
            self.fontData[code] = [0] * HEIGHT
        return self.fontData[code]

    def paint(self, event, value):
        if self.currentCode is None:
            return
        cx = min(WIDTH - 1, max(0, event.x // ZOOM))
        by = min(HEIGHT - 1, max(0, event.y // ZOOM))
        bm = self.getBitmap(self.currentCode)
        bit = WIDTH - 1 - cx if not self.lsbLeft.get() else cx
        if value:
            bm[by] |= (1 << bit)
        else:
            bm[by] &= ~(1 << bit)
        self.renderCanvas()
        self.refreshCharGrid()
        self.updateStatus()

    def clearChar(self):
        if self.currentCode is None:
            return
        self.fontData[self.currentCode] = [0] * HEIGHT
        self.renderCanvas()
        self.refreshCharGrid()
        self.updateStatus()

    def renderCanvas(self):
        self.canvas.delete("all")
        if self.currentCode is None:
            return
        bm = self.fontData.get(self.currentCode, [0] * HEIGHT)
        for y in range(HEIGHT):
            for x in range(WIDTH):
                bit = WIDTH - 1 - x if not self.lsbLeft.get() else x
                on = (bm[y] >> bit) & 1
                if on:
                    self.canvas.create_rectangle(x * ZOOM, y * ZOOM,
                                                 (x + 1) * ZOOM, (y + 1) * ZOOM,
                                                 fill="black", outline="black")
        for x in range(1, WIDTH):
            self.canvas.create_line(x * ZOOM, 0, x * ZOOM, HEIGHT * ZOOM, fill="#cccccc")
        for y in range(1, HEIGHT):
            self.canvas.create_line(0, y * ZOOM, WIDTH * ZOOM, y * ZOOM, fill="#cccccc")

    def updateStatus(self):
        if self.currentCode is not None:
            ch = chr(self.currentCode) if 32 <= self.currentCode <= 126 else "?"
            self.status.config(
                text="当前字符: %s (码 %d)   |   已定义 %d 个字符" % (
                    ch, self.currentCode, len(self.fontData)))
        else:
            self.status.config(text="请选择字符")

    # ---------------- 文件读写 ----------------
    def parseFontData(self, data):
        """按每字符 17 字节解析：码 + 16 字节位图。返回 {code: [16]}。"""
        out = {}
        n = len(data)
        for i in range(0, n - 16, 17):
            code = data[i]
            out[code] = list(data[i + 1:i + 17])
        return out

    def importFontFile(self):
        """从 simple .FNT/.FON 位图字体导入。"""
        path = filedialog.askopenfilename(
            title="选择 .fon/.fnt 位图字体",
            filetypes=[("位图字体", "*.fon *.fnt"), ("所有文件", "*.*")])
        if not path:
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            messagebox.showerror("打开失败", str(e))
            return

        glyphs = self.parseFnt(data)
        if glyphs is None:
            glyphs = self.parseNeFon(data)
        if glyphs is None:
            messagebox.showerror("格式错误",
                                 "无法解析为 simple .FNT 位图字体，\n"
                                 "也不是 Windows NE(.fon) 位图字体。\n"
                                 "simple .FNT 头: [宽][高][字符数](小端 word)。")
            return

        startCode = askinteger("导入设置", "起始字符编码\n(第 1 个字形的码):",
                               initialvalue=0, minvalue=0, maxvalue=255)
        if startCode is None:
            return

        imported = 0
        for i, rows in enumerate(glyphs):
            code = startCode + i
            if code > 255:
                break
            bm = list(rows)
            if len(bm) > HEIGHT:
                bm = bm[:HEIGHT]
            else:
                bm = bm + [0] * (HEIGHT - len(bm))
            self.fontData[code] = bm
            imported += 1

        self.drawChars()
        self.selectCode(startCode)
        messagebox.showinfo("导入完成",
                            "从 %s 导入 %d 个字符\n"
                            "起始编码 %d，可用“保存”写回 font.bin。" % (
                                os.path.basename(path), imported, startCode))

    def parseFnt(self, data):
        """
        解析 simple .FNT/.FON 位图字体：
          头 6 字节: 宽(word) 高(word) 字符数(word)，均小端。
          其后每字符 = 高 行，每行 ceil(宽/8) 字节，像素 MSB=左。
        返回 list[list[byte]]，每字符按行取每行的第一个字节(8 位)；
        格式不匹配时返回 None。
        """
        if len(data) < 6:
            return None
        w, h, n = struct.unpack("<HHH", data[:6])
        if w == 0 or h == 0 or n == 0 or w > 32 or h > 32 or n > 256:
            return None
        bpr = (w + 7) // 8
        need = 6 + n * h * bpr
        if len(data) < need:
            return None

        glyphs = []
        off = 6
        for _ in range(n):
            rows = []
            for r in range(h):
                rows.append(data[off + r * bpr])   # 每行取其首字节的 8 位
            off += h * bpr
            glyphs.append(rows)
        return glyphs

    def parseNeFon(self, data):
        """
        解析 Windows NE(.fon) 位图字体，如 Bm437 系列(8x16)。

        .fon 带 MZ/NE 头，字形表位置不固定：内嵌一张 256 字符 × 16 字节
        的连续位图(MSB=左)。此处用一个稳定启发式扫描定位该表——
        大写字母、小写字母、数字的字体上半区(前 2 行)素为空。
        返回 list，每元素为含 16 字节的列表；找不到时返回 None。
        """
        if len(data) < 0x40 or data[:2] != b"MZ":
            return None
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 2 >= len(data) or data[e_lfanew:e_lfanew + 2] != b"NE":
            return None

        bestStart, bestScore = None, -1
        total = 256 * HEIGHT
        for start in range(0x40, len(data) - total + 1):
            table = data[start:start + total]
            score = 0
            for c in list(range(65, 91)) + list(range(97, 123)) + list(range(48, 58)):
                blk = table[c * HEIGHT:(c + 1) * HEIGHT]
                if sum(blk) == 0:
                    continue
                # 大写/小写/数字：字体的顶 2 行应留白
                score += 2 if (blk[0] == 0 and blk[1] == 0) else -3
            if sum(table[32 * HEIGHT:(32 + 1) * HEIGHT]) == 0:
                score += 1   # 空格(32)应为空
            if score > bestScore:
                bestScore, bestStart = score, start

        if bestStart is None:
            return None

        glyphs = []
        for c in range(256):
            rows = data[bestStart + c * HEIGHT:bestStart + (c + 1) * HEIGHT]
            glyphs.append(list(rows))
        return glyphs

    def loadFont(self, fname):
        try:
            with open(fname, "rb") as f:
                data = f.read()
        except OSError:
            return False
        self.fontData = self.parseFontData(data)
        self.loadedCodes = set(self.fontData.keys())
        if self.fontData:
            self.selectCode(sorted(self.fontData.keys())[0])
        else:
            self.selectCode(65)
        self.refreshCharGrid()
        return True

    def loadFontFile(self, fname):
        if os.path.exists(fname):
            if self.loadFont(fname):
                self.root.title("VortexOS 8x16 字体编辑器 - %s" % fname)
        else:
            self.selectCode(65)

    def encodeFontData(self):
        """把所有已定义字符按码升序打包成字节流(17 字节/字符)。"""
        blob = bytearray()
        for code in sorted(self.fontData.keys()):
            bm = self.fontData[code]
            bm = (bm + [0] * HEIGHT)[:HEIGHT]
            blob.append(code & 0xFF)
            blob.extend(bm)
        return bytes(blob)

    def saveFont(self):
        if not self.fontData:
            messagebox.showinfo("提示", "还没有任何字符，无法保存")
            return
        blob = self.encodeFontData()
        path = LOADED_PATH if LOADED_PATH else self.defaultPath()
        try:
            with open(path, "wb") as f:
                f.write(blob)
        except OSError as e:
            messagebox.showerror("保存失败", str(e))
            return
        self.root.title("VortexOS 8x16 字体编辑器 - %s" % path)
        messagebox.showinfo("保存成功",
                            "已保存 %d 个字符 (%d 字节)\n%s" % (
                                len(self.fontData), len(blob), path))

    def saveFontAs(self):
        path = filedialog.asksaveasfilename(defaultextension=".bin",
                                            filetypes=[("字库", "*.bin")])
        if not path:
            return
        blob = self.encodeFontData()
        try:
            with open(path, "wb") as f:
                f.write(blob)
        except OSError as e:
            messagebox.showerror("保存失败", str(e))
            return
        messagebox.showinfo("保存成功",
                            "已保存 %d 个字符 (%d 字节)\n%s" % (
                                len(self.fontData), len(blob), path))


def main():
    global LOADED_PATH
    if len(sys.argv) > 1:
        LOADED_PATH = os.path.abspath(sys.argv[1])
    root = tk.Tk()
    FontEditor(root)
    root.mainloop()


if __name__ == "__main__":
    main()