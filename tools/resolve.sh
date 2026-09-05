#!/bin/bash
# 通过 kernel.map 解析指向函数所在节的符号。用法: resolve.sh HEXEIP
cd "$(dirname "$0")/.."
MAP=kernel.map
TARGET=$1
# 找到最后一个地址 <= TARGET 的符号行
awk -v t="$TARGET" '
  /^ 0x[0-9a-f]{8}[ ]+.+$/ {
    addr=$1; sym=$2;
    gsub(/0x/,"",addr);
    if (("0x" addr) <= t) { lastaddr="0x" addr; lastsym=sym; }
  }
  END { print "target=" t "  ->  " lastaddr " " lastsym }
' "$MAP"
# 打印目标附近的行
grep -n "${TARGET%??}" "$MAP" | head -20