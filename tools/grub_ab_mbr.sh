#!/bin/bash
# A/B：标准 sfdisk MBR vs 我们的 hdd_mbr.bin，在同一 QEMU 下探测 part_msdos 能否枚举 msdos1。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbab
rm -rf "$T"; mkdir -p "$T"; cd "$T"

# (a) sfdisk 标准 MBR：64MB 盘，单 FAT32 分区自 2048 起
dd if=/dev/zero of=std.img bs=1M count=64 status=none
echo '2048, +, c' | sfdisk --force std.img >/dev/null 2>&1

# (b) 我们的 MBR 盘（boot.img + 分区表 = hdd_mbr.bin sector0），core.img 写 sector1
dd if=/dev/zero of=our.img bs=1M count=64 status=none
dd if="$SRC/iso/grub/hdd_mbr.bin" of=our.img conv=notrunc status=none
dd if="$SRC/iso/grub/core.img" of=our.img bs=512 seek=1 conv=notrunc status=none

probe() { # $1=img $2=label
  local img="$1"; local label="$2"
  rm -f sp.in sp.out; mkfifo sp.in sp.out
  exec 4<>sp.in
  ( cat < sp.out > "o_$label.txt" ) & cb=$!
  qemu-system-x86_64 -drive file="$img",format=raw,if=ide -boot c \
    -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
    -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
  qq=$!
  sleep 2
  printf 'insmod part_msdos\r' >&4
  printf 'ls\r' >&4
  printf 'ls (hd0)\r' >&4
  printf 'ls (hd0,msdos1)\r' >&4
  sleep 4
  kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
  echo "---------- $label ----------"
  cat -v "o_$label.txt" | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'grub>|error|Filesystem|msdos1|hd0|fd0' | grep -av 'Minimal'
}

probe std.img  "sfdisk_mbr"
probe our.img  "our_mbr"
echo "===== done ====="