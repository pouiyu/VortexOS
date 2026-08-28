#!/usr/bin/env python3
# make_cjk.py —— 从 Noto Sans CJK 光栅化 16×16 中文点阵，输出 Unicode 索引的 cjk16.bin。
#
# 输出格式（每条记录 36 字节，按码点升序）：
#   [0..3]    4 字节小端 Unciode 码点
#   [4..35]   32 字节 16×16 点阵：每行 2 字节(hi 为左半 8 列, lo 为右半 8 列)，
#             行内第 n 位(MSB=该半最左)对应该半第 n 列，共 16 行。
#
# 用法：
#   python3 make_cjk.py                 # 用脚本内置 TEXT 生成
#   python3 make_cjk.py 你的文本           # 用命令行文本的角色集生成(UTF-8)
#
# 仅对码点 >= 0x80 的字符生成字形；半角 ASCII 沿用现有 8×16 字体。
import os
import struct
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

# 构建期字体：WSL 的 Noto Sans CJK (19MB .ttc)。改为你自己的 CJK 字体路径亦可。
FONT_PATH = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "system", "font", "cjk16.bin")
SIZE = 16          # 输出点阵尺寸 16×16
RENDER = 64        # 先以更大尺寸渲染再缩小，保证小字号可读
THRESHOLD = 140    # 灰度阈值：> 阈值计为笔画像素

# 需要汉字的文本。可自行增删：最终生成的是这些字符去重后的点阵。
TEXT = ("VortexOS 中文显示系统 设置 关于 主题 光标 重启 关机 菜单 返回 "
        "信息 设备 格式化 键盘 鼠标 图形 系统 显示 你好 世界")


def collect_chars(text):
    chars = []
    for ch in text:
        cp = ord(ch)
        if cp < 0x80:
            continue
        if cp not in chars:
            chars.append(ch)
    return sorted(chars, key=ord)


def rasterize(ch):
    """把单个字符光栅成 16×16 二值点阵，返回 16 行，每行 16 个布尔像素。"""
    font = ImageFont.truetype(FONT_PATH, RENDER)
    img = Image.new("L", (RENDER, RENDER), 0)
    d = ImageDraw.Draw(img)
    # 先量出字形包围盒，居中放入画布
    bbox = d.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    ox = max(0, (RENDER - w) // 2 - bbox[0])
    oy = max(0, (RENDER - h) // 2 - bbox[1])
    d.text((ox, oy), ch, font=font, fill=255)
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    px = list(small.getdata())
    return [[px[r * SIZE + c] > THRESHOLD for c in range(SIZE)] for r in range(SIZE)]


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


def main():
    text = sys.argv[1] if len(sys.argv) > 1 else TEXT
    chars = collect_chars(text)
    if not chars:
        print("没有需要生成的汉字（全部 < 0x80 或为空）。")
        return 1

    blob = bytearray()
    for ch in chars:
        blob += struct.pack("<I", ord(ch))
        blob += pack(rasterize(ch))

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "wb") as f:
        f.write(blob)
    print(f"OK: {len(chars)} 字 -> {OUT_PATH} ({len(blob)} 字节)")
    for ch in chars[:0]:
        pass
    # 打印前几个字的起止，便于人肉核对
    n = 0
    for ch in chars:
        if n >= 6:
            print("  ...")
            break
        print("  U+%04X %s %s" % (ord(ch), ch, unicodedata.name(ch, "")))
        n += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())