#!/bin/bash
# A/B：不同分区表字节下（hd1）能否被 part_msdos 枚举 msdos1。hd0=GRUB 引导核固定。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
DBG=/tmp/gbdbg/dbgcore.img
T=/tmp/gbchs
rm -rf "$T"; mkdir -p "$T"; cd "$T"
dd if=/dev/zero of=grub.img bs=1M count=4 status=none
dd if="$SRC/iso/grub/hdd_mbr.bin" of=grub.img conv=notrunc status=none
dd if="$DBG" of=grub.img bs=512 seek=1 conv=notrunc status=none

mktable() { # $1=out  $2..=entry bytes(12)
  local out="$1"; shift
  dd if=/dev/zero of="$out" bs=1M count=64 status=none
  printf '\200' | dd of="$out" bs=1 seek=446 conv=notrunc status=none
  printf '%s' "$1" | xxd -r -p | dd of="$out" bs=1 seek=447 conv=notrunc status=none
}
# 4 个 variant 的分区表条目(自偏移447起，前3字节CHS开始，第4字节类型，后3字节CHS结束，后8字节LBA=2048,129024)
variant_probe() {
  local name="$1" bytes="$2"
  mktable "v.img" "$bytes"
  rm -f sp.in sp.out; mkfifo sp.in sp.out
  exec 4<>sp.in
  ( cat < sp.out > "o_$name.txt" ) & cb=$!
  qemu-system-x86_64 -drive file=grub.img,format=raw,if=ide \
    -drive file=v.img,format=raw,if=ide -boot c \
    -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
    -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
  qq=$!
  sleep 2
  printf 'insmod part_msdos\r' >&4; sleep 1
  printf 'insmod fat\r' >&4; sleep 1
  printf 'ls (hd1)\r' >&4; sleep 1
  printf 'ls (hd1,msdos1)\r' >&4; sleep 1
  printf 'set debug=partition\r' >&4; sleep 1
  printf 'ls (hd1,msdos1)\r' >&4; sleep 2
  kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
  local res
  res=$(cat -v "o_$name.txt" | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'msdos1|Filesystem|error|partition' | grep -av Minimal | tr '\n' '|')
  echo "[$name] $res"
}
variant_probe SENTINEL "feffff0cfeffff0008000000f80100"
variant_probe REALCHS  "2021000c2820080008000000f80100"
variant_probe ZEROCHS  "000000000000000008000000f80100"
variant_probe LBA0     "feffff0cfeffff0000000000f80100"
echo "===== done ====="