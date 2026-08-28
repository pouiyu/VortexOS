#!/usr/bin/env python3
import os, struct
p = os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","system","font","cjk16.bin")
data = open(p,"rb").read()
want = [0x4E2D,0x4F60,0x597D,0x5173,0x8BBE,0x56DE]  # 中你好关设回
m = {struct.unpack_from("<I",data,off)[0]:off for off in range(0,len(data),36)}
for cp in want:
    if cp not in m:
        print("missing",hex(cp)); continue
    off = m[cp]+4
    print("U+%04X:"%cp)
    for r in range(16):
        hi,lo = data[off+r*2],data[off+r*2+1]
        row=""
        for c in range(8):  row+=("#" if hi&(1<<(7-c)) else ".")
        for c in range(8):  row+=("#" if lo&(1<<(7-c)) else ".")
        print("  "+row)