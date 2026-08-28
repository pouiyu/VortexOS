#!/bin/bash
# 探测 hexdump/parttool 用法并尝试读 GRUB 眼中的 MBR 字节。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
DBG=/tmp/gbdbg/dbgcore.img
T=/tmp/gbhex
rm -rf "$T"; mkdir -p "$T"; cd "$T"
dd if=/dev/zero of=our.img bs=1M count=64 status=none
dd if="$SRC/iso/grub/hdd_mbr.bin" of=our.img conv=notrunc status=none
dd if="$DBG" of=our.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 4<>sp.in
( cat < sp.out > o.txt ) & cb=$!
qemu-system-x86_64 -drive file=our.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qq=$!
sleep 2
Q(){ printf '%s\r' "$1" >&4; sleep 1; }
Q "env -p"
Q "set"
Q "parttool (hd0)"
Q "parttool (hd0) type"
Q "insmod part_msdos"
Q "parttool (hd0)"
Q "ls (hd0,1)"
Q "ls (hd0,2)"
Q "ls (hd0,0)"
Q "configfile (hd0)/boot/grub/grub.cfg"
Q "configfile (hd0,msdos1)/boot/grub/grub.cfg"
sleep 3
kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
echo "===== log ====="
cat -v o.txt | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -70
echo "===== done ====="