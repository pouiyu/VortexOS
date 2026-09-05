#!/usr/bin/env python3
# make_cjk.py —— 从 Unifont 光栅化点阵字库，输出两份文件：
#   1) cjk16.bin  汉字 16×16（Unicode 索引）
#   2) font.bin   ASCII/扩展 8×16（图形模式 vbeDrawString 的英文部分）
#
# 输出格式（cjk16.bin，每条记录 36 字节，按码点升序）：
#   [0..3]    4 字节小端 Unicode 码点
#   [4..35]   32 字节 16×16 点阵：每行 2 字节(hi 为左半 8 列, lo 为右半 8 列)，
#             行内第 n 位(MSB=该半最左)对应该半第 n 列，共 16 行。
#
# 输出格式（font.bin，每条记录 17 字节，按码升序）：
#   [0]       字符码
#   [1..16]   16 字节 8×16 点阵：每行 1 字节，位 7 为最左列。
#
# 用法：
#   python3 make_cjk.py                 # 用脚本内置 TEXT 生成
#   python3 make_cjk.py 你的文本           # 用命令行文本的角色集生成(UTF-8)
#
# 中英文字体统一用 Unifont（点阵式开源字体，覆盖 ASCII/CJK）。Unifont 的
# 拉丁字形为 8×16 半宽单元，汉字为 16×16 全宽单元，正好契合本系统两种字形。
import os
import struct
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

# 构建期字体：Unifont（点阵风格，Win2000/嵌入式那种味道）。
# WSL 侧经 /mnt/c 访问用户桌面文件；Windows 本机 GUI 用同一 Windows 路径。
UNIFONT_WSL = "/mnt/c/Users/Administrator/Desktop/其他/Sucai/Unifont-v17.0.05/Unifont-v17.0.05/unifont-17.0.05.otf"
FONT_PATH = UNIFONT_WSL
FONT_INDEX = 0       # Unifont 只有 index 0
CJK_OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "system", "font", "cjk16.bin")
ASCII_OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "system", "font", "font.bin")
SIZE = 16          # 汉字点阵尺寸 16×16
RENDER = 112       # 用更高分辨率渲染，再缩到目标尺寸，保证点阵清晰
BINARIZE = 96      # 大图二值化阈值：> 阈值计为笔画。调高可把笔画削得更细
THRESHOLD = 120    # 缩放后最终二值化阈值：> 阈值计为笔画像素

# 需要汉字的文本。可自行增删：最终生成的是这些字符去重后的点阵。
TEXT = ("VortexOS 中文显示系统 设置 关于 主题 光标 重启 关机 菜单 返回 "
        "信息 设备 格式化 键盘 鼠标 图形 系统 显示 你好 世界")


def collect_chars(text):
    chars = []
    for ch in text:
        cp = ord(ch)
        if cp < 0x80:
            continue
        if ch not in chars:
            chars.append(ch)
    return sorted(chars, key=ord)


_fontCache = None


def getFont():
    global _fontCache
    if _fontCache is None:
        _fontCache = ImageFont.truetype(FONT_PATH, RENDER, index=FONT_INDEX)
    return _fontCache


def rasterize(ch):
    """把单个汉字光栅成 16×16 二值点阵，返回 16 行，每行 16 个布尔像素。

    分两步控制笔画粗细：
    1) 在大图(RENDER 分辨率)上先按 BINARIZE 阈值二值化，把抗锯齿边缘裁掉，
       得到细而干净的笔画骨架；
    2) 再缩放到 16×16，末了按 THRESHOLD 二值化落成点阵。
    这样不会像“灰阶直接缩小”那样糊成一条粗笔画，更贴近点阵字效果。
    """
    font = getFont()
    img = Image.new("L", (RENDER, RENDER), 0)
    d = ImageDraw.Draw(img)
    # 先量出字形包围盒，居中放入画布
    bbox = d.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    ox = max(0, (RENDER - w) // 2 - bbox[0])
    oy = max(0, (RENDER - h) // 2 - bbox[1])
    d.text((ox, oy), ch, font=font, fill=255)

    # 大图细线化：高阈值裁边缘，得到细笔画骨架(二值图)
    big = img.point(lambda v: 255 if v > BINARIZE else 0)

    # 整幅(含四边留白)等比缩放到 16x16。不要在此裁内容到铺满：
    # 汉字方块本就该占满 16 行，垂直位置交由绘制层 vbe 的 CJK_GLYPH_UPDAWN
    # 做整体平移与 ASCII 的视觉中段对齐。
    small = big.resize((SIZE, SIZE), Image.LANCZOS)
    px = list(small.getdata())

    return [[px[r * SIZE + c] > THRESHOLD for c in range(SIZE)] for r in range(SIZE)]


def rasterizeAscii(code):
    """把 ASCII/扩展码 code 光栅成 8×16 二值点阵，返回 16 行，每行 8 个布尔像素。

    Unifont 的拉丁字形正好占 8×16 半宽单元：设 k = RENDER/16，
    画布宽 8k、高 16k，字形在该单元内自左上铺开(实测 bbox 为 (0,0,8k,16k))。
    同样先大图二值化削细边缘，再缩到 8×16 落点阵。
    """
    font = getFont()
    k = RENDER // 16
    img = Image.new("L", (8 * k, 16 * k), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 0), chr(code), font=font, fill=255)

    big = img.point(lambda v: 255 if v > BINARIZE else 0)
    small = big.resize((8, SIZE), Image.LANCZOS)
    px = list(small.getdata())
    return [[px[r * 8 + c] > THRESHOLD for c in range(8)] for r in range(SIZE)]


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
    out = bytearray()
    for row in glyph:
        byte = 0
        for c in range(8):
            if row[c]:
                byte |= 1 << (7 - c)
        out.append(byte)
    return bytes(out)


def buildCjkFont(text):
    chars = collect_chars(text)
    blob = bytearray()
    for ch in chars:
        blob += struct.pack("<I", ord(ch))
        blob += pack(rasterize(ch))
    os.makedirs(os.path.dirname(CJK_OUT_PATH), exist_ok=True)
    with open(CJK_OUT_PATH, "wb") as f:
        f.write(blob)
    return chars, len(blob)


def buildAsciiFont():
    """生成 font.bin：码 0x20~0xFF 的 8×16 点阵(0x00~0x1F 控制符留空不写)。"""
    blob = bytearray()
    for code in range(0x20, 0x100):
        blob.append(code)
        blob += packAscii(rasterizeAscii(code))
    os.makedirs(os.path.dirname(ASCII_OUT_PATH), exist_ok=True)
    with open(ASCII_OUT_PATH, "wb") as f:
        f.write(blob)
    return len(blob)


def main():
    text = sys.argv[1] if len(sys.argv) > 1 else TEXT

    # 1) 汉字字库 cjk16.bin
    chars, cjkLen = buildCjkFont(text)
    if not chars:
        print("没有需要生成的汉字（全部 < 0x80 或为空），跳过 cjk16.bin。")
    else:
        print(f"OK: {len(chars)} 字 -> {CJK_OUT_PATH} ({cjkLen} 字节)")
        n = 0
        for ch in chars:
            if n >= 6:
                print("  ...")
                break
            print("  U+%04X %s %s" % (ord(ch), ch, unicodedata.name(ch, "")))
            n += 1

    # 2) ASCII 字库 font.bin
    asciiLen = buildAsciiFont()
    print(f"OK: 224 字符(0x20~0xFF) -> {ASCII_OUT_PATH} ({asciiLen} 字节)")
    return 0


if __name__ == "__main__":
    sys.exit(main())