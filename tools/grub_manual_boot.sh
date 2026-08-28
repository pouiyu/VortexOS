#!/bin/bash
# 用手动 GRUB 命令尝试真正拉起内核，判断 part_msdos+FAT 枚举是否可用。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbmb
rm -rf "$T"; mkdir -p "$T"; cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# Phase1 安装
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > p1.cap ) & c1=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -cdrom iso.iso -boot d \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
q1=$!
sleep 12; kill "$c1" 2>/dev/null; kill "$q1" 2>/dev/null; wait 2>/dev/null

# Phase2 手动引导
rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 4<>sp.in
( cat < sp.out > p2.cap ) & c2=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
q2=$!
sleep 2
Q(){ printf '%s\r' "$1" >&4; sleep 1; }
Q "insmod part_msdos"
Q "insmod fat"
Q "set root=(hd0,msdos1)"
Q "set prefix=(hd0,msdos1)/boot/grub"
Q "set debug=disk"
Q "multiboot2 /boot/kernel.bin"
Q "boot"
sleep 9
kill "$c2" 2>/dev/null; kill "$q2" 2>/dev/null; wait 2>/dev/null

echo "===== Phase2 manual boot log ====="
cat -v p2.cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -70
echo "===== done ====="