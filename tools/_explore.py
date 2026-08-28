# -*- coding: utf-8 -*-
import struct
FN = "Bm437_ACM_VGA_8x16.FON"
data = open(FN, "rb").read()

base = 0x47A  # 字形区起点 (offset[0])
n = 256
# 渲染 行式条带，每 8 个字符一行，展示 0x20..0x5f
def render(code):
    p = base + code * 16
    rows = [data[p + r] for r in range(16)]
    return rows

print("索引 0x20..0x5f (每索引一行16px, 竖排展示)")
start = 0x20
for code in range(start, 0x60):
    rows = render(code)
    print("%02x:%s" % (code, "".join(
        "#" if (rows[r] >> 7) or (rows[r] >> 6) & 1 or (rows[r] >> 5) & 1 or (rows[r] >> 4) & 1 or (rows[r] >> 3) & 1 or (rows[r] >> 2) & 1 or (rows[r] >> 1) & 1 or (rows[r] & 1) else "." for r in range(16))))