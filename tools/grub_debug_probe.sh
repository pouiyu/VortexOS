#!/bin/bash
# GRUB debug 日志：开启 debug=partition,disk 后 ls 看分区扫描过程。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbdbg
rm -rf "$T"; mkdir -p "$T"; cd "$T"
dd if=/dev/zero of=our.img bs=1M count=64 status=none
dd if="$SRC/iso/grub/hdd_mbr.bin" of=our.img conv=notrunc status=none
dd if="$SRC/iso/grub/core.img" of=our.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 4<>sp.in
( cat < sp.out > o.txt ) & cb=$!
qemu-system-x86_64 -drive file=our.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qq=$!
sleep 2
printf 'set debug=partition,disk\r' >&4
printf 'ls\r' >&4
printf 'ls (hd0)\r' >&4
printf 'ls (hd0,msdos1)\r' >&4
printf 'insmod part_msdos\r' >&4
printf 'ls\r' >&4
printf 'ls (hd0)\r' >&4
printf 'ls (hd0,msdos1)\r' >&4
sleep 6
kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
echo "===== full (debug) log ====="
cat -v o.txt | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -100
echo "===== done ====="